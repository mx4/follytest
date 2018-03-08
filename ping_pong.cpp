#include <chrono>

#include <folly/fibers/Fiber.h>
#include <folly/fibers/FiberManager.h>
#include <folly/fibers/SimpleLoopController.h>

using namespace folly::fibers;

int main(int argc, char* argv[])
{
   const uint64_t numIters = 30 * 1000 * 100;
   volatile bool done = false;
   Baton b1;
   Baton b2;

   FiberManager manager(std::make_unique<SimpleLoopController>());
   auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

   auto fib1 = [&] {
      while (!done) {
         b2.post();
         b1.wait();
      }
   };
   auto fib2 = [&] {
      uint64_t n = 0;
      while (n < numIters) {
         b2.wait();
         b1.post();
         n++;
      }
      done = true;
      loopController.stop();
   };

   auto loopFunc = [&]() {
      manager.addTask(fib1);
      manager.addTask(fib2);
   };

   auto t0 = std::chrono::steady_clock::now();

   loopController.loop(loopFunc);

   auto elapsed = std::chrono::steady_clock::now() - t0;
   auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
   auto latency = elapsed / numIters / std::chrono::nanoseconds(1) * 1.0;

   printf("done in %ld msec\n"
          "latency: ~%.1f nsec/switch\n",
          millis, latency);

   return 0;
}
