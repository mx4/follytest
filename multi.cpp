#include <stdio.h>

#include <string.h>

#include <thread>

#include <folly/fibers/Fiber.h>
#include <folly/fibers/FiberManagerMap.h>
#include <folly/init/Init.h>

#include <folly/Function.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <folly/io/async/EventBaseManager.h>

using namespace folly;

struct FiberManagerThread {
   EventBase        *evb;
   folly::Baton<>   *stop;
   std::thread       th;
};

static void
FiberFunc(void)
{
   printf("Yo.\n");
}


/*
 * Each thread function.
 */
static void
ManagerFunc(FiberManagerThread *manager,
            int                 idx)
{
   auto ebm = EventBaseManager::get();

   printf("thread: %d\n", idx);

   ebm->setEventBase(manager->evb, false);

   printf("before.\n");
   folly::fibers::getFiberManager(*manager->evb).addTask(FiberFunc);
   printf("after.\n");

   manager->evb->loopForever();

   EventBase::StackFunctionLoopCallback cb([=] { ebm->clearEventBase(); });

   manager->evb->runOnDestruction(&cb);
   manager->stop->wait();
   delete manager->evb;
   manager->evb = nullptr;
   delete manager->stop;
   manager->stop = nullptr;

   printf("thread: %d gone.\n", idx);
}


/*
 * Entry point.
 */
int
main(int argc, char* argv[])
{
   static const int numThreads = 4;
   FiberManagerThread managers[numThreads];

   folly::init(&argc, &argv);

   printf("init done.\n");

   for (auto i = 0; i < numThreads; i++) {
      managers[i].stop = new folly::Baton<>();
      managers[i].evb = new EventBase();
      managers[i].th = std::thread(ManagerFunc, &managers[i], i);
   }

   printf("all threads created.\n");

   for (auto i = 0; i < numThreads; i++) {
      managers[i].evb->waitUntilRunning();
   }

   printf("all threads settled.\n");

   for (auto i = 0; i < numThreads; i++) {
      managers[i].evb->terminateLoopSoon();
      managers[i].stop->post();
      managers[i].th.join();
   }

   printf("all threads stopped.\n");

   printf("done.\n");
}

/*
int main(int argc, char* argv[])
{
   int numThreads = 4;
   folly::EventBase evb;

   folly::init(&argc, &argv);

   printf("before.\n");
   folly::fibers::getFiberManager(evb).addTask([&]() { f(nullptr); });
   printf("after.\n");

   for (size_t i = 0; i < 1000 * 1000; i++) {
      folly::fibers::getFiberManager(evb).addTask([&]() { f(nullptr); });
   }
   printf("sent.\n");
   evb.loop();
   printf("done.\n");
}
*/
