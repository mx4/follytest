#pragma once

#include <folly/io/async/EventBase.h>

#include "follib_int.h"

void follib_init();
void follib_exit();
void follib_quiesce();
void follib_run_loop(bool waitNoReady=true);
void follib_run_loop_until_no_ready();
void follib_stop_test();
uint32_t follib_get_num_managers();

typedef void FiberFunc();

void follib_run_in_all_managers(FiberFunc *func);

folly::EventBase *follib_get_evb(int idx = -1);
folly::fibers::FiberManager *follib_get_manager(int idx = -1);
