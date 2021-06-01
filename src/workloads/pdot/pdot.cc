// -*- mode: c++; c-basic-offset: 2; -*-

/**
 * @file   pdot.cc
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

#include "libivy.hh"

typedef uint8_t *byte_ptr;

using namespace libivy;

using std::optional;
using std::span;
using std::vector;

constexpr static size_t NODES_WORKER = 2;

constexpr static char CANARY_VAL[] = "DEADBEEFBAADBEEF";

struct shm_hdr {
  char canary[sizeof(CANARY_VAL)];
  uint64_t elems;      /* Total num of elements in shm */
  uint8_t ready;       /* Signal ready to workers */
  uint64_t result[NODES_WORKER];
  uint8_t done[NODES_WORKER]; /* Signal completion to manager */

  /* Pad to make the header take up 1 page of space */
  uint8_t padding[libivy::Ivy::PAGE_SZ
		  - (sizeof(CANARY_VAL)
		     + sizeof(uint64_t) * (3)
		     + sizeof(shm_hdr::result)
		     + sizeof(shm_hdr::done))];
};

static_assert(sizeof(shm_hdr) == libivy::Ivy::PAGE_SZ);

struct shm_layout {
  shm_hdr header;
  uint64_t vecs[]; /* Vector A and B concatenated */
};

optional<shm_layout *> shm;
size_t region_sz = 0;

void dump_shm(size_t id) {
  return;
  
  std::cout << "SHM.data[" << id << "] = " << std::endl;
  size_t line_cnt = 0;
  for (size_t iter = 0; iter < shm.value()->header.elems/2; iter++) {
    std::cout << "0x" << std::setw(8) << std::setfill('0')
	      << shm.value()->vecs[id*shm.value()->header.elems/2 + iter]
	      << " ";
    
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
  while (shm.value()->header.ready != 1) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  
  size_t elems = shm.value()->header.elems/2;
  size_t elems_per_node = (elems/NODES_WORKER);
  
  size_t startA = elems_per_node * id;
  size_t startB = elems + elems_per_node * id;

  std::cout << "Elems per node = " << elems_per_node
	    << std::endl;

  uint64_t *vec_ptr = shm.value()->vecs;
  
  /* Get a view into the shared memory this worker will sort */
  span<uint64_t> vecA{&vec_ptr[startA], elems_per_node};
  span<uint64_t> vecB{&vec_ptr[startB], elems_per_node};

  /* Use multiply the values */
  auto start_time = std::chrono::high_resolution_clock::now();
  uint64_t sum = 0;
  for (size_t i = 0; i < elems_per_node; i++) {
    sum += vecA[i] * vecB[i];
  }
  auto end_time = std::chrono::high_resolution_clock::now();
  
  auto time_diff = end_time - start_time;
  auto time_elapsed
    = std::chrono::duration_cast<std::chrono::seconds>(time_diff);
  
  std::cout << "Time taken = " << time_elapsed.count()
	    << " s"
	    << std::endl;  

  shm.value()->header.result[id] = sum;
  
  /* Signal ready */
  std::cout << "Writing done to location "
	    << P(&shm.value()->header.done[id])
	    << " for id " << id
	    << std::endl;
  
  shm.value()->header.done[id] = 1;
  
  std::cout << "Multiply completed at " << id << std::endl;
}

uint64_t merge_worker() {
  uint64_t result = 0;

  for (size_t i = 0; i < NODES_WORKER; i++) {
    std::cout << "Worker " << i << "'s result = "
	      << shm.value()->header.result[i] << std::endl;
    result += shm.value()->header.result[i];
  }

  return result;
}

int main(int argc, char *argv[]) {
  std::string in_fname = "";
  std::string cfg_fname = "";
  uint64_t id = 0;
  
  if (argc != 4) {
    std::cout << "USAGE: " << argv[0]
	      << " <path to input file>"
	      << " <path to config file>"
	      << " <id>"
	      << std::endl;
    exit(1);
  } else {
    in_fname = std::string(argv[1]);
    cfg_fname = std::string(argv[2]);
    id = std::stoul(argv[3], nullptr, 10);
    
    std::cout << "Reading file " << in_fname << std::endl;
  }

  std::fstream in_f(in_fname);

  int in_num;
  size_t in_iter = 0;
  size_t elems;
  in_f >> elems;

  Ivy ivy(cfg_fname, id);
  auto [shm_, err_] = ivy.get_shm();

  if (err_.has_value()) assert(0 && "SHM failed");

  shm = reinterpret_cast<shm_layout*>(shm_);

  while (in_f >> in_num) {
    shm.value()->vecs[in_iter++] = in_num;
  }

  std::cout << "Read " << in_iter << " elems " << std::endl;
  
  shm.value()->header.elems = elems;
  shm.value()->header.ready = 0;

  for (size_t i = 0; i < NODES_WORKER; i++)
    shm.value()->header.done[i] = 0;

  dump_shm(0);
  dump_shm(1);

  shm.value()->header.ready = 1;

  /* If this is not the manager node*/
  if (id != 0) {
    mult_worker(id-1);    
  } else {
    /* Wait for every worker to complete */
    bool all_done = false;
    while (!all_done) {
      /* Wait for 100ms before checking */
      std::this_thread::sleep_for(std::chrono::seconds(10));

      all_done = true;

      for (size_t i = 0; i < NODES_WORKER; i++) {
	if (shm.value()->header.done[i] != 1) {
	  std::cout << "Checking for node " << i
		    << " at location "
		    << P(&shm.value()->header.done[i])
		    << std::endl;
	  std::cout << "id " << i << " is not done yet" << std::endl;
	  /* At least one node is not ready yet, wait for it */
	  all_done = false;
	  break;
	}
      }
    }
  }

  std::cout << "All done" << std::endl;

  if (id == 0)
    std::cout << "Dot product: " << merge_worker() << std::endl;

  while (1);
}
