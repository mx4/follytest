#include <pthread.h>

#include <thread>
#include <sstream>
#include <algorithm>

#include <folly/fibers/Fiber.h>
#include <folly/fibers/FiberManager.h>
#include <folly/fibers/EventBaseLoopController.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/io/async/EventHandler.h>
#include <folly/experimental/io/AsyncIO.h>
#include <folly/system/ThreadName.h>
#include <folly/Memory.h>

#define PAGE_SIZE 4096

#include "follib.h"

using namespace folly::fibers;

using folly::EventBase;
using folly::EventHandler;
using folly::AsyncIO;
using folly::AsyncIOOp;

struct AIOEventHandler;

/*
 * The state of per-thread fiber manager.
 */
struct fiber_mgr {
   FiberManager *manager{nullptr};
   EventBase     evb;
   Baton         baton;
   std::thread  *th{nullptr};
   uint32_t      idx;

   AsyncIOOp        asyncOp;
   AsyncIO         *asyncIO{nullptr};
   int              asyncIOFd{-1};
   AIOEventHandler *aioEventHandler{nullptr};
};

static struct {
   std::vector<fiber_mgr *> managers;
} libState;

static thread_local fiber_mgr *threadLocalMgr;


struct AIOEventHandler : public EventHandler {
   AIOEventHandler(EventBase *eb, int fd) : EventHandler(eb, fd) { }

   void handlerReady(uint16_t events) noexcept override {
      fiber_mgr *mgr = threadLocalMgr;

      assert(events == EventHandler::READ);

      auto completedOps = mgr->asyncIO->pollCompleted();
      assert(completedOps.size() == 1);

      mgr->baton.post();
   }
};



static uint32_t
get_num_cpus()
{
   return std::min(4u, std::thread::hardware_concurrency());
}


/*
 * follib_prw --
 *
 *      Generic r/w function.
 */
bool
follib_prw(bool     isRead,
           int      fd,
           uint64_t offset,
           uint32_t length,
           void    *buf)
{
   fiber_mgr *mgr = threadLocalMgr;

//   printf("mgr %u: %s: fd:%d off=%lu len: %u buf: 0x%p\n",
//          mgr->idx, __func__, fd, offset, length, buf);
   mgr->asyncOp.reset();
   if (isRead) {
      mgr->asyncOp.pread(fd, buf, length, offset);
   } else {
      mgr->asyncOp.pwrite(fd, buf, length, offset);
   }

   mgr->asyncIO->submit(&mgr->asyncOp);

   mgr->baton.wait();
   mgr->baton.reset();

   return true;
}


/*
 * follib_thread_func --
 *
 *      Each thread manager function.
 */
static void
follib_thread_func(fiber_mgr *mgr)
{
   assert(mgr->asyncIOFd != -1);
   assert(threadLocalMgr == nullptr);

   threadLocalMgr = mgr;
   folly::setThreadName(pthread_self(), "follib-mgr");

   printf("thread: %u starting\n", mgr->idx);

   mgr->baton.post();
   mgr->evb.loopForever();

   printf("thread: %u exited loopForever (hasTasks: %u)\n",
          mgr->idx, mgr->manager->hasTasks());

   while (mgr->manager->hasTasks()) {
      mgr->evb.loopOnce();
   }
   printf("thread: %u exiting\n", mgr->idx);
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

   printf("%s: %u threads\n", __func__, num_cpus);

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

      mgr->th = new std::thread(follib_thread_func, mgr);

      mgr->baton.wait();
      mgr->baton.reset();

      libState.managers.push_back(mgr);
   }

   printf("%s: ready.\n", __func__);
}


/*
 * follib_exit --
 *
 *      Stop processing fibers and shutdown all the threads.
 */
void
follib_exit()
{
   printf("%s: stopping all threads.\n", __func__);

   for (auto&& mgr : libState.managers) {
      mgr->evb.terminateLoopSoon();
      mgr->th->join();

      delete mgr->th;
      delete mgr->manager;
      delete mgr->asyncIO;
      delete mgr->aioEventHandler;
      delete mgr;
   }
   libState.managers.clear();

   printf("%s: done.\n", __func__);
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
