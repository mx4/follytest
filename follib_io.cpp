#include <pthread.h>
#include <stdio.h>

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
#include "follib_int.h"

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

   FLOG(2, "mgr %u: %s: %s fd:%d off: %7lu len: %5u\n",
        mgr->idx, __func__, isRead ? "read " : "write",
        fd, offset, length);

   mgr->asyncOp.reset();
   if (isRead) {
      mgr->asyncOp.pread(fd, buf, length, offset);
   } else {
      mgr->asyncOp.pwrite(fd, buf, length, offset);
   }

   mgr->asyncIO->submit(&mgr->asyncOp);

   mgr->baton.wait();
   mgr->baton.reset();

   ssize_t res = mgr->asyncOp.result();

   return res == length;
}


