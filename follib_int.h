#include <cstdint> // uint32_t
#include <thread>

#pragma once

#include <folly/fibers/Fiber.h>
#include <folly/fibers/FiberManager.h>
#include <folly/experimental/io/AsyncIO.h>
#include <folly/io/async/EventBaseManager.h>

struct AIOEventHandler;

class FollibReadCBs;

/*
 * The state of per-thread fiber manager.
 */
struct fiber_mgr {
   folly::fibers::FiberManager *manager{nullptr};
   folly::EventBase             evb;
   folly::fibers::Baton         baton;
   std::thread                 *th{nullptr};
   uint32_t                     idx;

   folly::AsyncIOOp    asyncOp;
   folly::AsyncIO     *asyncIO{nullptr};
   int                 asyncIOFd{-1};
   AIOEventHandler    *aioEventHandler{nullptr};
   FollibReadCBs      *readCB{nullptr};
};


void Log(const char *fmt, ...);

extern uint32_t logLevel;

fiber_mgr *follib_get_mgr();


#define FLOG(_lvl, _fmt, ...)    \
   do {                          \
      if (_lvl <= logLevel) {    \
         Log(_fmt, __VA_ARGS__); \
      }                          \
   } while (0)

