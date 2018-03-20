#include <folly/io/async/EventHandler.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/experimental/io/AsyncIO.h>

#include "follib_io.h"
#include "follib_int.h"

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
   fiber_mgr *mgr = follib_get_mgr();

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


