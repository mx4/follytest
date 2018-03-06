#include <thread>
#include <vector>
#include <sstream>
#include <cassert>
#include <iostream>

#include <folly/fibers/Fiber.h>
#include <folly/fibers/FiberManagerMap.h>
#include <folly/init/Init.h>

#include <folly/Function.h>
#include <folly/io/async/EventBaseManager.h>


using namespace folly;

struct fiber_mgr {
   EventBase   *evb;
   Baton<>     *stop;
   std::thread  th;
   uint32_t     idx;
};

struct fiber_state {
   uint32_t                 num_managers{0};
   std::vector<fiber_mgr *> managers;
};

static struct fiber_state state;


/*
 * The function invoked in the context of a fiber.
 */
static void
fiber_func(void)
{
   std::ostringstream ss;

   ss << std::this_thread::get_id();
   printf("fiber running in thread: %s\n", ss.str().c_str());
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
manager_func(fiber_mgr *manager)
{
   // optional
   if (1) {
      auto ebm = EventBaseManager::get();
      EventBase::StackFunctionLoopCallback cb([=] { ebm->clearEventBase(); });
      ebm->setEventBase(manager->evb, false);
      manager->evb->runOnDestruction(&cb);
   }

   printf("thread: %u starting\n", manager->idx);

   manager->evb->loopForever();
   manager->stop->wait();

   printf("thread: %u exiting\n", manager->idx);
}


static void
fiber_init()
{
   uint32_t num_cpus = get_num_cpus();

   printf("fiber_init: %u threads\n", num_cpus);
   state.num_managers = num_cpus;

   for (uint32_t i = 0; i < num_cpus; i++) {
      auto manager = new fiber_mgr;

      manager->stop = new Baton<>();
      manager->evb  = new EventBase();
      manager->idx  = i;
      manager->th   = std::thread(manager_func, manager);

      state.managers.push_back(manager);
   }

   for (auto&& manager : state.managers) {
      manager->evb->waitUntilRunning();
   }
   printf("fiber_init: ready.\n");
}

static void
fiber_exit()
{
   printf("fiber_exit: stopping all threads.\n");

   for (auto&& manager : state.managers) {
      manager->evb->terminateLoopSoon();
      manager->stop->post();
      manager->th.join();

      delete manager->evb;
      delete manager->stop;
      delete manager;
   }
   state.managers.clear();

   printf("fiber_exit: done.\n");
}

static void
fiber_run_in_each_manager()
{
   for (auto&& manager : state.managers) {
      fibers::getFiberManager(*manager->evb).addTaskRemote(fiber_func);
   }
}

/*
 * Entry point.
 */
int
main(int argc, char* argv[])
{
   init(&argc, &argv);

   fiber_init();
   fiber_run_in_each_manager();
   fiber_exit();

   return 0;
}
