#include <thread>
#include <iostream>

#include <folly/fibers/Fiber.h>
#include <folly/fibers/FiberManagerMap.h>
#include <folly/init/Init.h>

#include <folly/Function.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <folly/io/async/EventBaseManager.h>

using namespace folly;

struct FiberManagerThread {
   EventBase    *evb;
   Baton<>      *stop;
   std::thread   th;
};

static void
FiberFunc(void)
{
   std::cout << "Yo." << std::endl;
}


/*
 * Each thread function.
 */
static void
ManagerFunc(FiberManagerThread *manager,
            int                 idx)
{
   auto ebm = EventBaseManager::get();

   std::cout << "thread: " << idx << std::endl;

   ebm->setEventBase(manager->evb, false);

   printf("before.\n");
   fibers::getFiberManager(*manager->evb).addTask(FiberFunc);
   printf("after.\n");

   manager->evb->loopForever();

   EventBase::StackFunctionLoopCallback cb([=] { ebm->clearEventBase(); });

   manager->evb->runOnDestruction(&cb);
   manager->stop->wait();
   delete manager->evb;
   manager->evb = nullptr;
   delete manager->stop;
   manager->stop = nullptr;

   std::cout << "thread: " << idx << " gone." << std::endl;
}


/*
 * Entry point.
 */
int
main(int argc, char* argv[])
{
   static const int numThreads = 4;
   FiberManagerThread managers[numThreads];

   init(&argc, &argv);

   std::cout << "init done." << std::endl;

   for (auto i = 0; i < numThreads; i++) {
      managers[i].stop = new folly::Baton<>();
      managers[i].evb = new EventBase();
      managers[i].th = std::thread(ManagerFunc, &managers[i], i);
   }

   std::cout << "all threads created." << std::endl;

   for (auto i = 0; i < numThreads; i++) {
      managers[i].evb->waitUntilRunning();
   }

   std::cout << "all threads settled." << std::endl;

   for (auto i = 0; i < numThreads; i++) {
      managers[i].evb->terminateLoopSoon();
      managers[i].stop->post();
      managers[i].th.join();
   }

   std::cout << "all threads stopped." << std::endl;
   std::cout << "done." << std::endl;
}
