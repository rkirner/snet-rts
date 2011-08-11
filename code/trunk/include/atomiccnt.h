/*****************************************************************************
 * Atomic counters
 *
 * Allow for atomic memory access and
 * fetch-and-increment and fetch-and-decrement
 * operations.
 *
 * Following datatypes and operations are supported:
 *
 * snet_atomiccnt_t               datatype for atomic counter
 * SNET_ATOMICCNT_INITIALIZER(i)  statically initialize with i
 *
 * Set an atomic counter to a specified value
 *    void SNetAtomicCntSet(snet_atomiccnt_t *cnt, int val);
 *
 * Atomic fetch-and-increment
 *    int SNetAtomicCntFetchAndInc(snet_atomiccnt_t *cnt);
 *
 * Atomic fetch-and-decrement
 *    int SNetAtomicCntFetchAndDec(snet_atomiccnt_t *cnt);
 *
 *
 * Auth: Daniel Prokesch <dlp@snet-home.org>
 * Date: 2011/08/02
 *
 ****************************************************************************/
#ifndef _ATOMICCNT_H_
#define _ATOMICCNT_H_


#ifdef HAVE_SYNC_ATOMIC_BUILTINS

typedef struct {
  volatile int counter;
  unsigned char padding[64-sizeof(int)];
} snet_atomiccnt_t;

#define SNET_ATOMICCNT_INITIALIZER(i)  {(i)}

static inline void SNetAtomicCntSet(snet_atomiccnt_t *cnt, int val)
{ (void) __sync_lock_test_and_set(&cnt->counter, val); }

static inline int SNetAtomicCntFetchAndInc(snet_atomiccnt_t *cnt)
{ return __sync_fetch_and_add(&cnt->counter, 1); }


static inline int SNetAtomicCntFetchAndDec(snet_atomiccnt_t *cnt)
{ return __sync_fetch_and_sub(&cnt->counter, 1); }



#else /* HAVE_SYNC_ATOMIC_BUILTINS */


#include <pthread.h>

typedef struct {
  int counter;
  pthread_mutex_t lock;
} snet_atomiccnt_t;

#define SNET_ATOMICCNT_INITIALIZER(i)  {(i),PTHREAD_MUTEX_INITIALIZER}



static inline void SNetAtomicCntSet(snet_atomiccnt_t *cnt, int val)
{
  (void) pthread_mutex_lock( &cnt->lock);
  cnt->counter = val;
  (void) pthread_mutex_unlock( &cnt->lock);
}

static inline int SNetAtomicCntFetchAndInc(snet_atomiccnt_t *cnt)
{
  int tmp;
  (void) pthread_mutex_lock( &cnt->lock);
  tmp = cnt->counter;
  cnt->counter += 1;
  (void) pthread_mutex_unlock( &cnt->lock);
  return tmp;
}

static inline int SNetAtomicCntFetchAndDec(snet_atomiccnt_t *cnt)
{
  int tmp;
  (void) pthread_mutex_lock( &cnt->lock);
  tmp = cnt->counter;
  cnt->counter -= 1;
  (void) pthread_mutex_unlock( &cnt->lock);
  return tmp;
}



#endif /* HAVE_SYNC_ATOMIC_BUILTINS */

#endif /* _ATOMICCNT_H_ */