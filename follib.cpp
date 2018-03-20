#include <stdio.h>
#include <signal.h>

#include <thread>

#include <folly/fibers/Fiber.h>
#include <folly/fibers/FiberManager.h>
#include <folly/fibers/EventBaseLoopController.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/io/async/EventHandler.h>
#include <folly/io/async/AsyncSignalHandler.h>
#include <folly/experimental/io/AsyncIO.h>
#include <folly/system/ThreadName.h>

#define PAGE_SIZE 4096

#include "follib.h"
#include "follib_int.h"

using namespace folly::fibers;

using folly::EventBase;
using folly::EventHandler;
using folly::AsyncIO;
using folly::AsyncIOOp;

uint32_t logLevel = 2;


static thread_local fiber_mgr *threadLocalMgr;

class SignalEventHandler : public folly::AsyncSignalHandler {
   using AsyncSignalHandler::AsyncSignalHandler;
   void signalReceived(int sig) noexcept override {
      fiber_mgr *mgr = threadLocalMgr;

      printf("%u: got signal %u\n", mgr->idx, sig);
      assert(mgr->idx == 0);
      follib_stop_test();
   }
};

/*
 * This is the event handler we register for the eventfd that is used to signal
 * the end of a file i/o.
 */
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


static struct {
   SignalEventHandler      *sigHandler;
   std::vector<fiber_mgr *> managers;
} libState;


uint32_t
follib_get_num_managers()
{
   return libState.managers.size();
}

void
follib_stop_test()
{
   auto evb = follib_get_evb(0);

   /*
    * this function is ok to call from any thread as per the doc.
    */
   evb->terminateLoopSoon();
}


fiber_mgr *
follib_get_mgr()
{
   assert(threadLocalMgr);
   return threadLocalMgr;
}


EventBase *
follib_get_evb(int idx)
{
   if (idx == -1) {
      auto mgr = follib_get_mgr();
      return &mgr->evb;
   }

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


void
follib_run_loop_until_no_ready()
{
   auto mgr = follib_get_mgr();

   while (mgr->manager->hasTasks()) {
      mgr->evb.loopOnce();
   }
   FLOG(1, "thread %u: no ready tasks left.\n", mgr->idx);
}


void
follib_run_loop(bool waitNoReady)
{
   auto mgr = follib_get_mgr();

   mgr->evb.loopForever();

   FLOG(1, "thread: %u exited loopForever (hasTasks: %u)\n",
        mgr->idx, mgr->manager->hasTasks());

   if (waitNoReady) {
      follib_run_loop_until_no_ready();
   } else {
      FLOG(1, "thread: %u: %s done.\n", mgr->idx, __func__);
   }
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
   mgr->evb.setName(threadName);

   FLOG(1, "thread: %u starting\n", mgr->idx);

   follib_run_loop(true);

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

      mgr->evb.setName("");
      mgr->manager = new FiberManager(std::make_unique<EventBaseLoopController>(),
                                      options);
      dynamic_cast<EventBaseLoopController&>(mgr->manager->loopController())
               .attachEventBase(mgr->evb);
      mgr->idx = i;
      mgr->asyncIO = new AsyncIO(numMaxAsyncIO, AsyncIO::POLLABLE);
      mgr->asyncIOFd = mgr->asyncIO->pollFd();
      mgr->aioEventHandler = new AIOEventHandler(&mgr->evb, mgr->asyncIOFd);

      mgr->aioEventHandler->registerHandler(EventHandler::READ |
                                            EventHandler::PERSIST);
      if (i == 0) {
         assert(!mgr->th);
         threadLocalMgr = mgr;
         libState.sigHandler = new SignalEventHandler(&mgr->evb);
         libState.sigHandler->registerSignalHandler(SIGINT);
      } else {
         mgr->th = new std::thread(follib_thread_func, mgr);
         // wait til the poll loop is running in the new thread
         mgr->evb.waitUntilRunning();
      }

      libState.managers.push_back(mgr);
   }


   FLOG(1, "%s: ready.\n", __func__);
}


/*
 * follib_quiesce --
 *
 *      Quiesce all the threads.
 */
void
follib_quiesce()
{
   FLOG(1, "%s: quiescing all threads.\n", __func__);

   for (auto&& mgr : libState.managers) {
      if (mgr->idx == 0) {
         continue;
      }
      mgr->evb.terminateLoopSoon();
      mgr->th->join();
      delete mgr->th;
      mgr->th = nullptr;
   }
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

   /*
    * The shutdown code takes care of quiescing all the threads and then
    * deallocating the event-base and other structs. Another alternative
    * consists in having each thread tear down the part of the infrastructure
    * they use.
    */
   for (auto&& mgr : libState.managers) {
      if (mgr->idx != 0) {
         mgr->evb.terminateLoopSoon();
         if (mgr->th) {
            mgr->th->join();
         }
      }

      delete mgr->th;
      mgr->th = nullptr;
      delete mgr->manager;
      mgr->manager = nullptr;
      delete mgr->asyncIO;
      mgr->asyncIO = nullptr;
      delete mgr->aioEventHandler;
      mgr->aioEventHandler = nullptr;
      if (mgr->idx == 0) {
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


FiberManager *
follib_get_manager(int idx)
{
   if (idx == -1) {
      auto mgr = follib_get_mgr();
      return mgr->manager;
   }
   return libState.managers.at(idx)->manager;
}
