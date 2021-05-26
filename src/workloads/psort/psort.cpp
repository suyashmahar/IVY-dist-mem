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

using libivy::Ivy;
using std::optional;
using std::span;
using std::vector;

constexpr static size_t NODES = 4;
constexpr static pid_t CHILD_PID = 0;

struct shm_hdr {
  uint64_t elems;      /* Total num of elements in shm */
  uint8_t ready;       /* Signal ready to workers */
  uint8_t done[NODES]; /* Signal completion to manager */
  uint64_t nodes;      /* Total number of nodes */
};

struct shm_layout {
  shm_hdr header;
  uint64_t data[];
};

optional<shm_layout *> shm;
size_t region_sz = 0;

void dump_shm() {
  std::cout << "SHM.data = :" << std::endl;
  size_t line_cnt = 0;
  for (size_t iter = 0; iter < shm.value()->header.elems; iter++) {
    std::cout << "0x" << std::setw(8) << std::setfill('0') << shm.value()->data[iter] << " "; 
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
void sort_worker(size_t id) {
  /* Busy waiting on ready */
  while (!shm.value()->header.ready) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout << "Ready value = " << (int)(shm.value()->header.ready)
	    << std::endl;
  
  size_t elems = shm.value()->header.elems;
  size_t elems_per_node = (elems/shm.value()->header.nodes);
  
  size_t start = elems_per_node * id;

  uint64_t *data_ptr = shm.value()->data;
  
  /* Get a view into the shared memory this worker will sort */
  span<uint64_t> workset{&data_ptr[start], elems_per_node};

  /* Use qsort on the region */
  std::sort(workset.begin(), workset.end(), std::greater<uint64_t>{});

  /* Signal ready */
  shm.value()->header.done[id] = 1;
  
  std::cout << "Sorting completed at " << id << std::endl;
}

void merge_worker() {
  size_t iter[NODES];

  for (size_t i = 0; i < NODES; i++) {
    iter[i] = 0;
  }

  auto next_iter = [&iter](size_t node) {
    if (iter[node] < shm.value()->header.elems) {
      iter[node]++;
    }
  };

  auto done = [&iter]() -> bool {
    for (size_t i = 0; i < NODES; i++) {
      if (iter[i] != shm.value()->header.elems/NODES){
	return false;
      }
    }
    
    return true;
  };
  
  while (!done()) {
    vector<uint64_t> cur_elems(NODES);

    uint64_t max_elem = 0;
    uint64_t max_idx;
    for (size_t i = 0; i < NODES; i++) {
      if (iter[i] != shm.value()->header.elems/NODES) {
	// std::cout << "offset = " << i*ELEMS + iter[i] << std::endl;
	 auto elem = shm.value()->data[i*shm.value()->header.elems/NODES + iter[i]];
	  if (elem >= max_elem){
	    max_idx = i;
	    max_elem = elem;
	  }
	}
    }

    std::cout << max_elem << " for " << max_idx << std::endl;

    next_iter(max_idx);    
  }
}

void setup_shm(Ivy &ivy, std::string in_fname) {
  std::fstream in_f(in_fname);
  size_t elems;  
  in_f >> elems;

  region_sz = sizeof(uint64_t)*elems + sizeof(shm_hdr);
  auto [shm_, err] = ivy.get_shm();

  if (err.has_value()) {
    throw std::runtime_error("get_shm(): " + err.value());
  }

  DBGH << "Memsetting range " << shm_ << " + "
       << region_sz << std::endl;

  // DBGH << "First value = " << ((uint64_t*)shm)[0] << std::endl;
  // std::memset(shm_, 0, region_sz);

  // ivy.request_lock(shm_, 4096);


  shm = reinterpret_cast<shm_layout*>(shm_);
}

void populate_shm(Ivy &ivy, std::string in_fname) {
  std::memset(shm.value(), 0, region_sz);
  
  std::fstream in_f(in_fname);
  
  int in_num;
  size_t in_iter = 0;
  size_t elems;
  
  in_f >> elems;
  while (in_f >> in_num) {
    shm.value()->data[in_iter++] = in_num;
  }

  std::cout << "Read " << in_iter << " elems " << std::endl;
  
  shm.value()->header.elems = elems;
  shm.value()->header.ready = 0;

  for (size_t i = 0; i < NODES; i++)
    shm.value()->header.done[i] = 0;

  shm.value()->header.nodes = NODES;

  dump_shm();

  shm.value()->header.ready = 1;
}

void wait_for_workers() {
  /* Wait for every worker to complete */
  bool all_done = false;
  size_t seclap = 0;
  
  while (!all_done) {
    /* Wait for 100ms before checking */
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    all_done = true;

    for (size_t i = 0; i < NODES; i++) {
      if (shm.value()->header.done[i] != 1) {
	/* At least one node is not ready yet, wait for it */
	all_done = false;
	break;
      }
    }

    seclap += 1;
    std::cout << seclap << " seconds elapsed waiting for workers" << std::endl;
  }
}

int main(int argc, char *argv[]) {
  std::string in_fname = "";
  std::string cfg_fname = "";
  uint64_t id = 0;
  
  if (argc != 4) {    
    std::cout << "USAGE: " << argv[0]
	      <<  " <path to input file> <path to config file> <id>"
	      << std::endl;
    exit(1);
  } else {
    in_fname = std::string(argv[1]);
    cfg_fname = std::string(argv[2]);
    id = std::strtoull(argv[3], NULL, 0);
  }

  Ivy ivy(cfg_fname, id);

  auto is_manager_res = ivy.is_manager();
  auto is_manager = false;
  
  if (is_manager_res.second.has_value()) {
    throw is_manager_res.second.value();
  } else {
    is_manager = is_manager_res.first;
  }
  
  std::cout << "Is manager = " << is_manager << std::endl;

  setup_shm(ivy, in_fname);
    
  if (is_manager) {
    populate_shm(ivy, in_fname);
    wait_for_workers();
    merge_worker();
  } else {
    sort_worker(id);
  }
  
  std::cout << "All done" << std::endl;

  using namespace std::chrono_literals;
  
  // std::this_thread::sleep_for(24h);
  while (1);
}
