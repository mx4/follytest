#pragma once

#include <folly/io/async/EventBase.h>

void follib_init();
void follib_exit();

typedef void FiberFunc();

folly::EventBase *follib_get_evb(uint32_t idx);

void follib_run_in_all_managers(FiberFunc *func);
