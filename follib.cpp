#include <pthread.h>
#include <stdio.h>
#include <signal.h>

#include <thread>
#include <sstream>
#include <algorithm>

#include <folly/fibers/Fiber.h>
#include <folly/fibers/FiberManager.h>
#include <folly/fibers/EventBaseLoopController.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/io/async/EventHandler.h>
#include <folly/io/async/AsyncSignalHandler.h>
#include <folly/experimental/io/AsyncIO.h>
#include <folly/system/ThreadName.h>
#include <folly/Memory.h>

#define PAGE_SIZE 4096

#include "follib.h"
#include "follib_int.h"

using namespace folly::fibers;

using folly::EventBase;
using folly::EventHandler;
using folly::AsyncIO;
using folly::AsyncIOOp;

struct AIOEventHandler;
struct SignalEventHandler;

uint32_t logLevel = 2;


/*
 * The state of per-thread fiber manager.
 */
struct fiber_mgr {
   FiberManager *manager{nullptr};
   EventBase     evb;
   Baton         baton;
   std::thread  *th{nullptr};
   uint32_t      idx;

   AsyncIOOp           asyncOp;
   AsyncIO            *asyncIO{nullptr};
   int                 asyncIOFd{-1};
   AIOEventHandler    *aioEventHandler{nullptr};
};

thread_local fiber_mgr *threadLocalMgr;

class SignalEventHandler : public folly::AsyncSignalHandler {
   using AsyncSignalHandler::AsyncSignalHandler;
   void signalReceived(int sig) noexcept override {
      fiber_mgr *mgr = threadLocalMgr;

      printf("%u: got signal %u\n", mgr->idx, sig);
   }
};

static struct {
   SignalEventHandler      *sigHandler;
   std::vector<fiber_mgr *> managers;
} libState;


struct AIOEventHandler : public EventHandler {
   AIOEventHandler(EventBase *eb, int fd) : EventHandler(eb, fd) { }

   void handlerReady(uint16_t events) noexcept override {
      fiber_mgr *mgr = threadLocalMgr;

      DCHECK_EQ(events, EventHandler::READ);

      auto completedOps = mgr->asyncIO->pollCompleted();
      DCHECK_EQ(completedOps.size(), 1);

      mgr->baton.post();
   }
};


EventBase *
follib_get_evb(uint32_t idx)
{
   fiber_mgr *mgr = libState.managers.at(idx);

   return &mgr->evb;
}

static uint32_t
get_num_cpus()
{
   return std::min(4u, std::thread::hardware_concurrency());
}


/*
 * Log --
 *
 *      Log routine.
 */
void
Log(const char *fmt,
    ...)
{
   va_list ap;

   va_start(ap, fmt);
   vprintf(fmt, ap);
   va_end(ap);
}


/*
 * follib_thread_func --
 *
 *      Each thread manager function.
 */
static void
follib_thread_func(fiber_mgr *mgr)
{
   char threadName[16];
   DCHECK_NE(mgr->asyncIOFd, -1);
   DCHECK(threadLocalMgr == nullptr);

   threadLocalMgr = mgr;
   snprintf(threadName, sizeof threadName, "follib-mgr-%u", mgr->idx);
   folly::setThreadName(threadName);

   FLOG(1, "thread: %u starting\n", mgr->idx);

   mgr->evb.loopForever();

   FLOG(1, "thread: %u exited loopForever (hasTasks: %u)\n",
        mgr->idx, mgr->manager->hasTasks());

   while (mgr->manager->hasTasks()) {
      mgr->evb.loopOnce();
   }
   FLOG(1, "thread: %u exiting\n", mgr->idx);
   threadLocalMgr = nullptr;
}


/*
 * follib_init --
 *
 *      Starts multiple threads and initialize the logic required to run
 *      fibers.
 */
void
follib_init()
{
   const uint32_t numMaxAsyncIO = 32;
   const uint32_t num_cpus = get_num_cpus();

   auto options = FiberManager::Options();
   options.stackSize = 4 * PAGE_SIZE;

   Log("%s: %u threads\n", __func__, num_cpus);

   for (uint32_t i = 0; i < num_cpus; i++) {
      auto mgr = new fiber_mgr;

      mgr->manager = new FiberManager(std::make_unique<EventBaseLoopController>(),
                                      options);
      dynamic_cast<EventBaseLoopController&>(mgr->manager->loopController())
               .attachEventBase(mgr->evb);
      mgr->idx = i;
      mgr->asyncIO = new AsyncIO(numMaxAsyncIO, AsyncIO::POLLABLE);
      mgr->asyncIOFd = mgr->asyncIO->pollFd();
      mgr->aioEventHandler = new AIOEventHandler(&mgr->evb, mgr->asyncIOFd);

      mgr->aioEventHandler->registerHandler(EventHandler::READ
                                            | EventHandler::PERSIST);
      if (i == 0) {
         libState.sigHandler = new SignalEventHandler(&mgr->evb);
         libState.sigHandler->registerSignalHandler(SIGINT);
      }

      mgr->th = new std::thread(follib_thread_func, mgr);

      // wait til the poll loop is running in the new thread
      mgr->evb.waitUntilRunning();

      libState.managers.push_back(mgr);
   }

   FLOG(1, "%s: ready.\n", __func__);
}


/*
 * follib_exit --
 *
 *      Stop processing fibers and shutdown all the threads.
 */
void
follib_exit()
{
   FLOG(1, "%s: stopping all threads.\n", __func__);

   for (uint32_t i = 0; i < libState.managers.size(); i++) {
      auto mgr = libState.managers[i];

      mgr->evb.terminateLoopSoon();
      mgr->th->join();

      delete mgr->th;
      delete mgr->manager;
      delete mgr->asyncIO;
      delete mgr->aioEventHandler;
      if (i == 0) {
         delete libState.sigHandler;
         libState.sigHandler = nullptr;
      }
      delete mgr;
   }
   libState.managers.clear();

   Log("%s: done.\n", __func__);
}


/*
 * follib_run_in_all_managers --
 *
 *      Routine used to run a fiber in all the threads.
 */
void
follib_run_in_all_managers(FiberFunc *func)
{
   for (auto&& mgr : libState.managers) {
      mgr->manager->addTaskRemote(func);
   }
}
