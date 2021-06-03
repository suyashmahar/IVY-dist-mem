// -*- mode: c++; c-basic-offset: 2; -*-

/**
 * @file   sigsegvlatency.cc
 * @date   May 30, 2021
 * @brief  Brief description here
 */

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

constexpr static size_t PAGE_SZ = 4096; // Bytes
constexpr static size_t ITERATIONS = 16384 * 128;

using void_ptr = void *;
using byte_ptr = uint8_t *;

using namespace std::chrono;

uint8_t *scratch_page;

enum Permission { RD = 0, WR = 1, RW = 2, NONE = 3 };

#define ABORT(msg)					\
  do {std::cerr << "ABORT: " << (msg) << std::endl;	\
    assert(0);						\
  } while (0);

#define P_ABORT(msg)					\
  do {							\
    std::cerr << std::string(std::strerror(errno))	\
	      << std::endl;				\
    ABORT(msg);						\
  } while (0);


void set_perm(void_ptr page, Permission perm) {
  int prot = 0;

  switch (perm) {
  case Permission::NONE:
    prot = PROT_NONE;
    break;
  case Permission::RD:
    prot = PROT_READ;
    break;
  case Permission::WR:
    prot = PROT_WRITE;
    break;
  case Permission::RW:
    prot = PROT_READ | PROT_WRITE;
    break;
  default:
    ABORT("Unknown permission call");
  }

  int res = mprotect(page, PAGE_SZ, prot);

  if (res == -1) P_ABORT("mprotect failed");
    
}

void lock_page(void_ptr page) { set_perm(page, Permission::NONE); }

void unlock_page(void_ptr page) { set_perm(page, Permission::RW); }

void sigaction_hdlr(int sig, siginfo_t *info, void *ctx_ptr) {
  auto addr = reinterpret_cast<void_ptr>(info->si_addr);

  /* Unlock the fault page and return */
  unlock_page(addr);

  return;
}

int main(int argc, char *argv[]) {
  /* Allocate a aligned page */
  void_ptr pg_addr = mmap(nullptr, PAGE_SZ, PROT_READ | PROT_WRITE,
			  MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
  
  std::cerr << "pg_addr = " << pg_addr << std::endl;

  if (pg_addr == nullptr) P_ABORT("mmap failed");

  scratch_page = reinterpret_cast<uint8_t*>(pg_addr);

  
  /*****************************************
   * Measure mprotect overhead
   *****************************************/

  auto perm_start = high_resolution_clock::now();

  /* Repeat couple of times to make sure clock's resolution doesn't
     affect measurements */
  for (size_t i = 0; i < ITERATIONS; i++) {
    lock_page(scratch_page);
    unlock_page(scratch_page);
  }

  auto perm_end = high_resolution_clock::now();

  auto perm_over = (perm_end - perm_start);
  auto perm_over_us = duration_cast<microseconds>(perm_over).count();

  /* Next lock the page and measure fault time to write a single
     byte */
  std::cout << "perm_overhead = "
	    << perm_over_us/float(ITERATIONS)
	    << std::endl;

  /*****************************************
   * Next calculate the sigaction overhead
   *****************************************/

  /* Install the fault handler */
  struct sigaction act {};
  act.sa_sigaction = &sigaction_hdlr;
  sigfillset(&act.sa_mask);
  act.sa_flags = SA_RESTART | SA_SIGINFO;

  if (sigaction(SIGSEGV, &act, NULL) == -1) 
    P_ABORT("sigaction failed");

    
  /* Run the loop */
  
  auto fault_timer_start = high_resolution_clock::now();
  for (size_t i = 0; i < ITERATIONS; i++) {
    lock_page(scratch_page);
    scratch_page[0] = 0;
  }
  auto fault_timer_end = high_resolution_clock::now();

  auto fault_dur = fault_timer_end - fault_timer_start;
  auto fault_dur_us = duration_cast<microseconds>(fault_dur).count();

  std::cout << "fault_overhead = "
	    << (fault_dur_us)/float(ITERATIONS)
	    << std::endl;

  std::cout << "signal overhead = "
	    << (fault_dur_us-perm_over_us)/float(ITERATIONS)
	    << std::endl;    

  return 0;
}
