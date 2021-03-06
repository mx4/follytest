#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

#include <sstream>

#include <folly/Memory.h>

#include "follib.h"
#include "follib_io.h"

#define PAGE_SIZE 4096

#include "test_file_io.h"


/*
 * Test state.
 */
static struct {
   const char *fileName{"/tmp/multi.dat"};
   int         fileFd{-1};
   size_t      fileSize{1024 * 1024};

   ssize_t     allocSize{64 * 1024};
   size_t      ioSize{4 * 1024};
   uint32_t    numTotalIOs{256};
} testState;


static void
test_prepare_file()
{
   const ssize_t allocSize = testState.allocSize;
   uint8_t *buf;
   int fd;

   printf("preparing file '%s'.\n", testState.fileName);
   fd = ::open(testState.fileName, O_RDWR | O_CREAT | O_DIRECT, 0755);
   if (fd < 0) {
      printf("failed to open file: %s\n", strerror(errno));
      goto error;
   }

   buf = (uint8_t *)folly::aligned_malloc(allocSize, PAGE_SIZE);
   assert(buf);

   for (uint32_t i = 0; i < testState.fileSize / allocSize; i++) {
      memset(buf, (uint8_t)i, allocSize);
      auto res = pwrite(fd, buf, allocSize, i * allocSize);
      if (res != allocSize) {
         printf("failed to write: %zu\n", res);
         goto error;
      }
   }
   testState.fileFd = fd;
   folly::aligned_free(buf);
   return;
error:
   exit(1);
}


static void
test_close_file()
{
   if (testState.fileFd < 0) {
      return;
   }
   printf("closing file.\n");
   ::close(testState.fileFd);
   testState.fileFd = -1;
}


static void
fiber_test_func(int fibIdx)
{
   assert(testState.fileFd > 0);

   for (uint32_t i = 0; i < testState.numTotalIOs; i++) {
      uint32_t j = rand() + i;
      size_t ioSize = testState.ioSize * ((j % 4) + 1);
      uint8_t *buf;
      uint32_t off;
      bool res;

      if (follib_need_exit()) {
         printf("thread %u: fiber %d interrupted.\n", follib_get_mgr_idx(), fibIdx);
         return;
      }

      buf = (uint8_t *)folly::aligned_malloc(ioSize, PAGE_SIZE);

      off = (rand() * 1024) % testState.fileSize;
      if (off + ioSize > testState.fileSize) {
         off -= ioSize;
      }

      bool isRead = (j & 3) != 0;
      if (!isRead) {
         memset(buf, (uint8_t) j, ioSize);
      }
      res = follib_prw(isRead, testState.fileFd, off, ioSize, buf);
      assert(res);
      (void) res;

      folly::aligned_free(buf);
   }
   printf("thread %u: fiber %d done.\n", follib_get_mgr_idx(), fibIdx);
}


static void
test_run_func_in_each_manager(uint32_t numFibs)
{
   printf("launching %u fibers.\n", numFibs);

   for (uint32_t i = 0; i < numFibs; i++) {
      follib_run_in_all_managers([i]() { fiber_test_func(i); });
   }
}


void
test_file_io()
{
   printf("----- %s -----\n", __func__);
   follib_init();

   test_prepare_file();

   test_run_func_in_each_manager(10);

   follib_run_loop_until_no_ready();

   follib_quiesce();

   test_close_file();

   follib_exit();
}
