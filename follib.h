#pragma once

#include <folly/io/async/EventBase.h>
#include <folly/fibers/FiberManager.h>
#include <folly/fibers/Fiber.h>

void follib_init();
void follib_exit();
void follib_quiesce();
void follib_run_loop(bool waitNoReady=true);
void follib_run_loop_until_no_ready();
void follib_stop_test();
bool follib_need_exit();
uint32_t follib_get_num_managers();
uint32_t follib_get_mgr_idx();

folly::EventBase *follib_get_evb(int idx = -1);
folly::fibers::FiberManager *follib_get_manager(int idx = -1);

template <typename F>
inline void
follib_run_in_all_managers(F&& func)
{
   const uint32_t n = follib_get_num_managers();

   for (uint32_t i = 0; i < n; i++) {
      auto manager = follib_get_manager(i);

      manager->addTaskRemote(func);
   }
}
