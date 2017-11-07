#include <thread>
#include <iostream>
#include <vector>
#include <cassert>

#include <folly/fibers/Fiber.h>
#include <folly/fibers/FiberManagerMap.h>
#include <folly/init/Init.h>

#include <folly/Function.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <folly/io/async/EventBaseManager.h>

using namespace folly;

struct FiberManagerThread {
   EventBase   *evb;
   Baton<>     *stop;
   std::thread  th;
};


struct fiber_state {
   uint32_t                          num_cpus{0};
   std::vector<FiberManagerThread *> managers;
};

static struct fiber_state state;

/*
 * The function invoked in the context of a fiber.
 */
static void
fiber_func(void)
{
   auto id = std::this_thread::get_id();
   std::cout << "Fiber running in thread: " << id << std::endl;
}


static uint32_t
get_num_cpus()
{
   return std::thread::hardware_concurrency();
}


/*
 * Each thread function.
 */
static void
manager_func(FiberManagerThread *manager,
             int                 idx)
{
   auto ebm = EventBaseManager::get();

   std::cout << "thread: " << idx << std::endl;

   ebm->setEventBase(manager->evb, false);

   fibers::getFiberManager(*manager->evb).addTask(fiber_func);

   manager->evb->loopForever();

   EventBase::StackFunctionLoopCallback cb([=] { ebm->clearEventBase(); });

   manager->evb->runOnDestruction(&cb);
   manager->stop->wait();

   delete manager->evb;
   delete manager->stop;

   manager->evb  = nullptr;
   manager->stop = nullptr;

   std::cout << "thread: " << idx << " gone." << std::endl;
}


static void
fiber_init()
{
   uint32_t num_cpus = get_num_cpus();

   std::cout << "init: " << num_cpus << " threads" <<  std::endl;

   for (auto i = 0; i < num_cpus; i++) {
      auto manager = new FiberManagerThread;

      manager->stop = new folly::Baton<>();
      manager->evb = new EventBase();
      manager->th = std::thread(manager_func, manager, i);

      state.managers.push_back(manager);
   }

   std::cout << "all threads created." << std::endl;

   for (auto i = 0; i < num_cpus; i++) {
      state.managers[i]->evb->waitUntilRunning();
   }
   std::cout << "all threads settled." << std::endl;

}

static void
fiber_exit()
{
   std::cout << "stopping all threads." << std::endl;

   for (auto i = 0; i < state.num_cpus; i++) {
      auto manager = state.managers[i];

      manager->evb->terminateLoopSoon();
      manager->stop->post();
      manager->th.join();

      assert(manager->evb == nullptr);
      assert(manager->stop == nullptr);

      delete manager;
   }
   state.managers.clear();

   std::cout << "all threads stopped." << std::endl;
   std::cout << "done." << std::endl;
}


/*
 * Entry point.
 */
int
main(int argc, char* argv[])
{
   init(&argc, &argv);

   fiber_init();

   /*
    * do something here.
    */

   fiber_exit();
}
