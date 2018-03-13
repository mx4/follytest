#pragma once

#include <folly/SharedMutex.h>

/*
 * libfolly doesn't distinguish between mutex and rw locks. That makes our
 * life easier since we have a single type to manipulate.
 */

typedef folly::SharedMutexImpl<true> DSharedMutexReadPriority;

struct follib_rw_lock {
   DSharedMutexReadPriority lock;
};


static inline void
follib_rw_lock_init(follib_rw_lock *rwLock)
{
}

static inline void
follib_rw_lock_rd_lock(follib_rw_lock *rwLock)
{
   rwLock->lock.lock_shared();
}

static inline void
follib_rw_lock_rd_unlock(follib_rw_lock *rwLock)
{
   rwLock->lock.unlock_shared();
}

static inline void
follib_rw_lock_wr_lock(follib_rw_lock *rwLock)
{
   rwLock->lock.lock();
}

static inline void
follib_rw_lock_wr_unlock(follib_rw_lock *rwLock)
{
   rwLock->lock.unlock();
}

static inline void
follib_rw_lock_exit(follib_rw_lock *rwLock)
{
}



