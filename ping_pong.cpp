#include <thread>
#include <chrono>
#include <iostream>

#include <folly/fibers/Fiber.h>
#include <folly/fibers/FiberManagerMap.h>
#include <folly/init/Init.h>

using namespace folly;

int main(int argc, char* argv[])
{
   EventBase evb;
   fibers::Baton b1;
   fibers::Baton b2;
   const uint64_t numIters = 40 * 1000 * 1000;
   volatile bool done = false;

   init(&argc, &argv);

   std::cout << "starting.." << std::endl;
   auto t0 = std::chrono::steady_clock::now();
   fibers::getFiberManager(evb).addTask([&]() {
      while (!done) {
         b2.post();
         b1.wait();
      }
   });
   fibers::getFiberManager(evb).addTask([&]() {
      uint64_t n = 0;
      while (n < numIters) {
         b2.wait();
         b1.post();
         n++;
      }
      done = true;
   });

   evb.loop();

   auto elapsed = std::chrono::steady_clock::now() - t0;
   auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
   auto latency = elapsed / numIters / std::chrono::nanoseconds(1) * 1.0;

   std::cout << "done in " << millis << " msec" << std::endl;
   std::cout << "~ " << latency << " nsec / switch" << std::endl;

   return 0;
}
