#include <cstdint> // uint32_t
#include <thread>

#pragma once

#include <folly/fibers/Fiber.h>
#include <folly/fibers/FiberManager.h>
#include <folly/experimental/io/AsyncIO.h>
#include <folly/io/async/EventBaseManager.h>

struct AIOEventHandler;

/*
 * The state of per-thread fiber manager.
 */
struct fiber_mgr {
   std::unique_ptr<folly::fibers::FiberManager> manager;
   folly::EventBase                             evb;
   folly::fibers::Baton                         baton;
   std::unique_ptr<std::thread>                 th;
   uint32_t                                     idx{0};

   std::unique_ptr<folly::AsyncIO>   asyncIO;
   std::unique_ptr<AIOEventHandler>  aioEventHandler;
};


fiber_mgr *follib_get_mgr();

