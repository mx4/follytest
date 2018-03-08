#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include <thread>
#include <sstream>

#include <folly/Memory.h>

#include "follib.h"

#define PAGE_SIZE 4096


/*
 * Test state.
 */
static struct {
   const char *fileName{"/tmp/multi.dat"};
   int         fileFd{-1};
   size_t      fileSize{1024 * 1024};

   ssize_t     allocSize{64 * 1024};
   size_t      ioSize{4 * 1024};
   uint32_t    numTotalIOs{4 * 1024};
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
fiber_test_func()
{
   assert(testState.fileFd > 0);

   for (uint32_t i = 0; i < testState.numTotalIOs; i++) {
      uint8_t *buf = (uint8_t *)folly::aligned_malloc(testState.ioSize, PAGE_SIZE);
      uint32_t off;
      bool res;

      off = i * testState.ioSize;
      if (off + testState.ioSize > testState.fileSize) {
         off = 0;
      }

      res = follib_pread(testState.fileFd, off, testState.ioSize, buf);
      assert(res);

      folly::aligned_free(buf);
   }
   printf("fiber done.\n");
}


static void
test_run_func_in_each_manager()
{
   printf("running work fibers.\n");

   follib_run_in_all_managers(fiber_test_func);
}


int
main(int argc, char* argv[])
{
   follib_init();

   test_prepare_file();
   test_run_func_in_each_manager();

   follib_exit();

   test_close_file();

   return 0;
}
