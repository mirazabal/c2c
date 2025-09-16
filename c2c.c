#define _GNU_SOURCE
#include <sched.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>

#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

typedef struct{
  atomic_int* seq1;
  atomic_int* seq2;
  int nsamples;
  int cpu;
} arg_t;

static
int64_t time_now_ns(void)
{
  struct timespec tms = {0};
  if (clock_gettime(CLOCK_REALTIME,&tms)) {
    return -1;
  }
  int64_t nanos = tms.tv_sec * 1000000000;
  nanos += tms.tv_nsec;

  return nanos;
}

static
void pin_thread(int cpu) 
{
  cpu_set_t set = {0};
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  pid_t calling_thread = 0; 
  int const rc = sched_setaffinity(calling_thread, sizeof(set), &set);
  if(rc == -1){
    printf("cpu %d \n", cpu);
    printf("errno %d \n", errno);
  }
  assert(rc != -1 && "sched_setaffinity");
}

void* func(void* arg)
{
  arg_t* a =(arg_t*)arg;
 
  pin_thread(a->cpu);

  for (int m = 0; m < a->nsamples; ++m) {
    for (int n = 0; n < 128; ++n) {
      while (atomic_load_explicit(a->seq1, memory_order_acquire) != n)
        ;
      atomic_store_explicit(a->seq2, n, memory_order_release);
    }
  }
    return NULL;
}

static
void print_results(int64_t const* rtt_arr, size_t sz)
{
  assert(rtt_arr != NULL);

  char buff[128] = {0};
  int it_buff = 0;

  int rv = sprintf(buff + it_buff , "      ");
  it_buff += rv;

  for(size_t i = 0; i < sz; ++i){
     int rv = sprintf(buff + it_buff , "%3ld | ", i);
     it_buff += rv;
  }
  printf("%s\n", buff);
  memset(buff, 0, 128*sizeof(char));
  it_buff = 0;

  rv = sprintf(buff + it_buff , "     ");
  it_buff += rv;

  for(size_t i = 0; i < sz; ++i){
     int rv = sprintf(buff + it_buff , "------");
     it_buff += rv;
  }
  printf("%s\n", buff);
  memset(buff, 0, 128*sizeof(char));
  it_buff = 0;

  for (size_t i = 0; i < sz; ++i) {
   int rv = sprintf(buff + it_buff , "%3ld | ", i );
   it_buff += rv;

    for (size_t j = 0; j < sz; ++j) {
       int rv = sprintf(buff + it_buff , "%3ld | ", rtt_arr[i*sz + j]);
       it_buff += rv;
    } 
    printf("%s\n", buff);
    memset(buff, 0, 128*sizeof(char));
    it_buff = 0;
  }
}

int main()
{
  cpu_set_t set = {0};
  CPU_ZERO(&set);
  int rc = sched_getaffinity(0, sizeof(set), &set);
  assert(rc != -1 && "sched_getaffinity");

  int cpus[CPU_SETSIZE] = {0}; 
  size_t sz = 0;
  for(size_t i = 0; i < CPU_SETSIZE; ++i){
    if (CPU_ISSET(i, &set)) {
      cpus[sz++] = i;
    }
  }

  int64_t rtt_arr[sz][sz];
  memset(rtt_arr, 0, sz*sz*sizeof(int64_t));

  alignas(64) atomic_int seq1;
  alignas(64) atomic_int seq2;

  int nsamples = 1024;

  for(size_t r = 0; r < 2; r++){
    for (size_t i = 0; i < sz; ++i) {
      for (size_t j = i + 1; j < sz ; ++j) {
        // Secondary thread
        pthread_t p = {0};
        pthread_attr_t const* attr = NULL;
        arg_t arg = {.seq1 = &seq1, .seq2 = &seq2, .nsamples = nsamples, .cpu = cpus[j]};
        int rc = pthread_create(&p, attr, func, &arg);
        assert(rc == 0);

        // Primary thread
        pin_thread(cpus[i]);

        int64_t rtt = INT64_MAX;
        for (int m = 0; m < nsamples; ++m) {
          seq1 = seq2 = -1;
          int64_t const t0 = time_now_ns(); 
          for (int n = 0; n < 128; ++n) {
            atomic_store_explicit(&seq1, n, memory_order_release);
            while (atomic_load_explicit(&seq2, memory_order_acquire) != n)
              ;
          }
          int64_t const t1 = time_now_ns(); 
          rtt = min(rtt, t1 - t0);
        }
        rtt_arr[i][j] = rtt / 2 / 128;
        rtt_arr[j][i] = rtt / 2 / 128;
        printf("Latency from cpu %lu to cpu %lu %ld nsec \n", i,j,rtt / 2 / 128);
        pthread_join(p, NULL);
      }
    }
  }

  print_results(&rtt_arr[0][0], sz);

  return 0;
}
