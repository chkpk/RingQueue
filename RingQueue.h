#ifndef RINGQUEUE_H_
#define RINGQUEUE_H_

#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <assert.h>
#include <xmmintrin.h>

#define MemoryBarrier() __asm__ __volatile__ ("" : : :"memory") // no compiler reordering

template <typename T>
class RingQueue{

  public:

    RingQueue(uint32_t capacity);
    ~RingQueue();

    int init();
    int reset();

    int mutex_push(T * item);
    T * mutex_pop();

    int spin1_push(T * item);
    T * spin1_pop();

    int spin2_push(T * item);
    T * spin2_pop();

    int cas_push(T * item);
    T * cas_pop();

    int mixed1_push(T * item);
    T * mixed1_pop();

    int mixed2_push(T * item);
    T * mixed2_pop();

  private:

    uint32_t  capacity_;
    uint32_t  mask_; 
    uint32_t  head_;
    uint32_t  tail_;
    uint32_t  write_finished_;
    uint32_t  read_finished_;

    T**  data_;

    pthread_spinlock_t  spinlock_;
    pthread_mutex_t mutex_;
    uint32_t lock_;
};

template <typename T>
RingQueue<T>::RingQueue(uint32_t capacity):
  capacity_(capacity),
  mask_(capacity - 1),
  data_(NULL){
}

template <typename T>
RingQueue<T>::~RingQueue() {
  pthread_spin_destroy(&spinlock_);
  pthread_mutex_destroy(&mutex_);
  if( data_ != NULL)
    free(data_); 
}

template <typename T>
inline int RingQueue<T>::init() {
  assert(data_ == NULL);
  data_ = (T**) calloc(sizeof(T*),capacity_);
  if (data_ == NULL)
    return -1;

  pthread_spin_init(&spinlock_, 0);
  pthread_mutex_init(&mutex_, NULL);
  lock_ = 0u;
  head_ = 0u;
  tail_ = 0u;
  write_finished_ = 0u;
  read_finished_ = 0u;
  return 0;
}

template <typename T>
inline int RingQueue<T>::reset() {
  pthread_spin_destroy(&spinlock_);
  pthread_mutex_destroy(&mutex_);
  pthread_spin_init(&spinlock_, 0);
  pthread_mutex_init(&mutex_, NULL);
  lock_ = 0u;
  head_ = 0u;
  tail_ = 0u;
  write_finished_ = 0u;
  read_finished_ = 0u;
  return 0;
}

// mutex push
template <typename T>
inline int RingQueue<T>::mutex_push(T * item) {
  pthread_mutex_lock(&mutex_);
  if ((head_ - tail_) > mask_) {
    pthread_mutex_unlock(&mutex_);
    return -1;
  }
  data_[head_ & mask_] = item;
  ++ head_;
  pthread_mutex_unlock(&mutex_);
  return 0;
}

// mutex pop
template <typename T>
inline T * RingQueue<T>::mutex_pop() {
  pthread_mutex_lock(&mutex_);
  if (tail_ == head_) { 
    pthread_mutex_unlock(&mutex_);
    return (T*)NULL;
  }
  T* item = data_[tail_ & mask_];
  ++ tail_;
  pthread_mutex_unlock(&mutex_);
  return item;
}

// spin1 push
template <typename T>
inline int RingQueue<T>::spin1_push(T * item) {
  pthread_spin_lock(&spinlock_);
  if ((head_ - tail_) > mask_) {
    pthread_spin_unlock(&spinlock_);
    return -1;
  }
  data_[head_ & mask_] = item;
  ++ head_;
  pthread_spin_unlock(&spinlock_);
  return 0;
}

// spin1 pop
template <typename T>
inline T * RingQueue<T>::spin1_pop() {
  pthread_spin_lock(&spinlock_);
  if (tail_ == head_) { 
    pthread_spin_unlock(&spinlock_);
    return (T*)NULL;
  }
  T* item = data_[tail_ & mask_];
  ++ tail_;
  pthread_spin_unlock(&spinlock_);
  return item;
}

// spin2 push
template <typename T>
inline int RingQueue<T>::spin2_push(T * item) {
  while (__sync_lock_test_and_set(&lock_,1));
  if ((head_ - tail_) > mask_) {
    __sync_lock_release(&lock_);
    return -1;
  }
  data_[head_ & mask_] = item;
  ++ head_;
  __sync_lock_release(&lock_);
  return 0;
}

// spin2 pop
template <typename T>
inline T * RingQueue<T>::spin2_pop() {
  while (__sync_lock_test_and_set(&lock_,1));
  if (tail_ == head_) { 
    __sync_lock_release(&lock_);
    return (T*)NULL;
  }
  T* item = data_[tail_ & mask_];
  ++ tail_;
  __sync_lock_release(&lock_);
  return item;
}

// cas push
template <typename T>
inline int RingQueue<T>::cas_push(T * item) {
  uint32_t head, next;

  MemoryBarrier();
  do {
    if (head_ - read_finished_ > mask_)
      return -1;
    head = head_;
    next = head + 1;
  } while (!__sync_bool_compare_and_swap(&head_, head, next));
  data_[head & mask_] = item;
  MemoryBarrier();

  while (write_finished_ != head)
    _mm_pause();
  write_finished_ = next; 
  return 0;
}

// cas pop
template <typename T>
inline T * RingQueue<T>::cas_pop() {
  uint32_t tail, next;
  T *ret;

  MemoryBarrier();
  do {
    if (tail_ == write_finished_ ) 
      return (T*)NULL;
    tail = tail_;
    next = tail + 1;
  } while (!__sync_bool_compare_and_swap(&tail_, tail, next));
  ret = data_[tail & mask_];
  MemoryBarrier();

  while (read_finished_ != tail)
    _mm_pause();
  read_finished_ = next;
  return ret;
}

// mixed1 push
template <typename T>
inline int RingQueue<T>::mixed1_push(T * item) {
  uint32_t pause_cnt, loop_count, yield_cnt, spin_count;
  static const timespec tv = {0,1};

  if (__sync_lock_test_and_set(&lock_, 1u) != 0u) {
    yield_cnt = 0u;
    loop_count = 0u;
    spin_count = 1u;
    do {
      if (loop_count < 16u) { 
        for (pause_cnt = spin_count; pause_cnt > 0u; --pause_cnt) {
          _mm_pause();
        }
        spin_count *= 2; 
        ++loop_count;
      }else{
        if ((yield_cnt & 0xf) == 0xf) {
          nanosleep(&tv, NULL);  
        } else{ 
          sched_yield();        
        }
        ++yield_cnt;
      }
    } while (!__sync_bool_compare_and_swap(&lock_, 0u, 1u));
  }

  if ((head_ - tail_) > mask_) {
    __sync_lock_release(&lock_);
    return -1;
  }
  data_[head_ & mask_] = item;
  ++ head_;
  __sync_lock_release(&lock_);
  return 0;
}

// mixed1 pop
template <typename T>
inline T * RingQueue<T>::mixed1_pop() {
  uint32_t pause_cnt,loop_count, yield_cnt, spin_count;
  static const timespec tv = {0,1};

  if (__sync_lock_test_and_set(&lock_, 1u) != 0u) {
    yield_cnt = 0u;
    loop_count = 0u;
    spin_count = 1u;
    do {
      if (loop_count < 16u) { 
        for (pause_cnt = spin_count; pause_cnt > 0u; --pause_cnt) {
          _mm_pause();
        }
        spin_count *= 2;  
        ++loop_count;
      }else{
        if ((yield_cnt & 0xf) == 0xf) {
          nanosleep(&tv, NULL);          
        } else{ 
          sched_yield();       
        }
        ++yield_cnt;
      }
    } while (!__sync_bool_compare_and_swap(&lock_, 0u, 1u));
  }

  if (tail_ == head_){
    __sync_lock_release(&lock_);
    return (T*)NULL;
  }
  T* item = data_[tail_ & mask_];
  ++ tail_;
  __sync_lock_release(&lock_);
  return item;
}

#define MixedLock(lock)   do {                                          \
    uint32_t loop_count = 0u, spin_count = 1u, pause_cnt = 0u;          \
    if (__sync_lock_test_and_set(&(lock), 1u) != 0u) {                  \
      do {                                                              \
        if (loop_count < 17u) {                                         \
          for (pause_cnt = spin_count; pause_cnt > 0u; --pause_cnt) {   \
            _mm_pause();                                                \
          }                                                             \
          spin_count *= 2;                                              \
            ++loop_count;                                               \
        }else{                                                          \
          sched_yield();                                                \
        }                                                               \
      } while (!__sync_bool_compare_and_swap(&(lock), 0u, 1u));         \
    }                                                                   \
  }while (0)

/// mixed2 push
template <typename T>
inline int RingQueue<T>::mixed2_push(T * item) {
  MixedLock(lock_);
  if ((head_ - tail_) > mask_) {
    __sync_lock_release(&lock_);
    return -1;
  }
  data_[head_ & mask_] = item;
  ++ head_;
  __sync_lock_release(&lock_);
  return 0;
}

// mixed2 pop
template <typename T>
inline T * RingQueue<T>::mixed2_pop() {
  MixedLock(lock_);
  if (tail_ == head_){ 
    __sync_lock_release(&lock_);
    return (T*)NULL;
  }
  T* item = data_[tail_ & mask_];
  ++ tail_;
  __sync_lock_release(&lock_);
  return item;
}

#endif
