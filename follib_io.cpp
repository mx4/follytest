#include <folly/experimental/io/AsyncIO.h>

#include "follib_io.h"
#include "follib_int.h"
#include "follib.h"

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
   folly::fibers::Baton baton;
   folly::AsyncIOOp op;
   fiber_mgr *mgr = follib_get_mgr();

   FLOG(2, "mgr %u: %s: %s fd:%d off: %7lu len: %5u\n",
        mgr->idx, __func__, isRead ? "read " : "write",
        fd, offset, length);

   if (isRead) {
      op.pread(fd, buf, length, offset);
   } else {
      op.pwrite(fd, buf, length, offset);
   }

   op.setNotificationCallback([&baton](folly::AsyncIOOp *ioOp) { baton.post(); });

   mgr->asyncIO->submit(&op);

   baton.wait();

   return op.result() == length;
}


