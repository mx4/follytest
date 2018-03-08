#pragma once

typedef void FiberFunc(void);

void follib_init();
void follib_exit();

void follib_run_in_all_managers(FiberFunc *func);

bool
follib_prw(bool     isRead,
           int      fd,
           uint64_t offset,
           uint32_t length,
           void    *buf);

static inline bool
follib_pwrite(int      fd,
              uint64_t offset,
              uint32_t length,
              void    *buf)
{
   return follib_prw(false, fd, offset, length, buf);
}


static inline bool
follib_pread(int      fd,
             uint64_t offset,
             uint32_t length,
             void    *buf)
{
   return follib_prw(true, fd, offset, length, buf);
}


