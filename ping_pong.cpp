#include <stdio.h>
#include <string.h>
#include <thread>
#include <chrono>

#include <folly/fibers/Fiber.h>
#include <folly/fibers/FiberManagerMap.h>
#include <folly/init/Init.h>
#include <folly/Function.h>

using namespace folly;


int main(int argc, char* argv[])
{
   folly::EventBase evb;
   folly::fibers::Baton b1;
   folly::fibers::Baton b2;
   std::atomic<uint64_t> n{0};
   const uint64_t numIters = 2000 * 10000;

   folly::init(&argc, &argv);

   auto t0 = std::chrono::steady_clock::now();
   folly::fibers::getFiberManager(evb).addTask([&]() {
      while (n < numIters) {
         b2.post();
         b1.wait();
         n++;
      }
   });
   folly::fibers::getFiberManager(evb).addTask([&]() {
      while (n < numIters) {
         b2.wait();
         b1.post();
         n++;
      }
   });

   evb.loop();
   auto elapsed = std::chrono::steady_clock::now() - t0;
   printf("done in %.1f msec\n", elapsed / std::chrono::milliseconds(1) * 1.0);
   printf("-> %.1f nsec / switch\n", elapsed / numIters / std::chrono::nanoseconds(1) * 1.0);
}
