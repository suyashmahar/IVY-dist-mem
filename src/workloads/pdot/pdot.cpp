// -*- mode: c++; c-basic-offset: 2; -*-

/**
 * @file   pdot.cpp
 * @date   May 13, 2021
 * @brief  Brief description here
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

typedef uint8_t *byte_ptr;

using std::optional;
using std::span;
using std::vector;

constexpr static size_t NODES = 4;

struct shm_hdr {
  uint64_t elems;      /* Total num of elements in shm */
  uint8_t ready;       /* Signal ready to workers */
  uint8_t done[NODES]; /* Signal completion to manager */
  uint64_t result[NODES]; /* Result of the workers */
  uint64_t nodes;      /* Total number of nodes */
};

struct shm_layout {
  shm_hdr header;
  uint64_t vecs[]; /* Vector A and B concatenated */
};

optional<shm_layout *> shm;

void dump_shm(size_t id) {
  std::cout << "SHM.data[" << id << "] = " << std::endl;
  size_t line_cnt = 0;
  for (size_t iter = 0; iter < shm.value()->header.elems/2; iter++) {
    std::cout << "0x" << std::setw(8) << std::setfill('0')
	      << shm.value()->vecs[id*shm.value()->header.elems/2 + iter] << " ";
    
    if (++line_cnt > 10) {
      line_cnt = 0;
      std::cout << std::endl;
    }
    
  }
  std::cout << std::endl;
}

/**
 * @brief Sort the ith part of the shm
 * @param id Unique id of the worker
 */
void mult_worker(size_t id) {
  
  /* Busy waiting on ready */
  while (!shm.value()->header.ready) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  
  size_t elems = shm.value()->header.elems/2;
  size_t elems_per_node = (elems/shm.value()->header.nodes);
  
  size_t startA = elems_per_node * id;
  size_t startB = elems + elems_per_node * id;

  uint64_t *vec_ptr = shm.value()->vecs;
  
  /* Get a view into the shared memory this worker will sort */
  span<uint64_t> vecA{&vec_ptr[startA], elems_per_node};
  span<uint64_t> vecB{&vec_ptr[startB], elems_per_node};

  /* Use multiply the values */
  uint64_t sum = 0;
  for (size_t i = 0; i < elems_per_node; i++) {
    sum += vecA[i] * vecB[i];
  }

  shm.value()->header.result[id] = sum;
  
  /* Signal ready */
  shm.value()->header.done[id] = 1;
  
  std::cout << "Multiply completed at " << id << std::endl;
}

uint64_t merge_worker() {
  uint64_t result = 0;

  for (size_t i = 0; i < NODES; i++) {
    std::cout << "Worker " << i << "'s result = " << shm.value()->header.result[i] << std::endl;
    result += shm.value()->header.result[i];
  }

  return result;
}

int main(int argc, char *argv[]) {
  std::string in_fname = "";

  if (argc != 2) {
    std::cout << "USAGE: " << argv[0] <<  " <path to input file>"
	      << std::endl;
    exit(1);
  } else {
    in_fname = std::string(argv[1]);
    std::cout << "Reading file " << in_fname << std::endl;
  }

  std::fstream in_f(in_fname);

  int in_num;
  size_t in_iter = 0;
  size_t elems;
  in_f >> elems;

  size_t region_sz = sizeof(uint64_t)*elems + sizeof(shm_hdr);
  shm = new(malloc(region_sz)) shm_layout;

  while (in_f >> in_num) {
    shm.value()->vecs[in_iter++] = in_num;
  }

  std::cout << "Read " << in_iter << " elems " << std::endl;
  
  shm.value()->header.elems = elems;
  shm.value()->header.ready = false;

  for (size_t i = 0; i < NODES; i++)
    shm.value()->header.done[i] = 0;

  shm.value()->header.nodes = NODES;

  dump_shm(0);
  dump_shm(1);

  shm.value()->header.ready = true;

  /* Create n parallel processes */
  for (size_t i = 0; i < NODES; i++) {
    std::thread t(mult_worker, i);
    t.detach();
  }

  std::cout << "All children spawned" << std::endl;

  /* Wait for every worker to complete */
  bool all_done = false;
  while (!all_done) {
    /* Wait for 100ms before checking */
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    all_done = true;

    for (size_t i = 0; i < NODES; i++) {
      if (shm.value()->header.done[i] != 1) {
	std::cout << "id " << i << " is not done yet" << std::endl;
	/* At least one node is not ready yet, wait for it */
	all_done = false;
	break;
      }
    }
  }

  std::cout << "All done" << std::endl;
  std::cout << "Dot product: " << merge_worker() << std::endl;
}
