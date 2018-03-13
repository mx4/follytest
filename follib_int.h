#include <cstdint> // uint32_t

#pragma once

void Log(const char *fmt, ...);

extern uint32_t logLevel;

struct fiber_mgr;
extern thread_local fiber_mgr *threadLocalMgr;


#define FLOG(_lvl, _fmt, ...)    \
   do {                          \
      if (_lvl <= logLevel) {    \
         Log(_fmt, __VA_ARGS__); \
      }                          \
   } while (0)

