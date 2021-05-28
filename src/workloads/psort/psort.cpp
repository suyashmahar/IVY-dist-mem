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

constexpr static size_t NODES_TOTAL = 2;
constexpr static size_t NODES_WORKER = 1;

constexpr static pid_t CHILD_PID = 0;
constexpr static char CANARY_VAL[] = "DEADBEEFBAADBEEF";

#define ASSERT_VALID						\
  (memcmp(shm.value()->header.canary, CANARY_VAL, sizeof(CANARY_VAL)) != 0 && "SHM corrupted")

struct shm_hdr {
  char canary[sizeof(CANARY_VAL)];
  uint64_t elems;      /* Total num of elements in shm */
  uint8_t ready;       /* Signal ready to workers */
  uint8_t done[NODES_WORKER]; /* Signal completion to manager */
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
    std::cout << "Waiting for the ready signal" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }

  ASSERT_VALID;

  std::cout << "Ready value = " << (int)(shm.value()->header.ready)
	    << std::endl;
  
  size_t elems = shm.value()->header.elems;
  
  ASSERT_VALID;
  size_t elems_per_node = (elems/shm.value()->header.nodes);

  std::cout << "&elems = " << &shm.value()->header.elems << std::endl;
  std::cout << "elems = " << elems << std::endl;
  
  size_t start = elems_per_node * id;

  uint64_t *data_ptr = shm.value()->data;
  
  /* Get a view into the shared memory this worker will sort */
  span<uint64_t> workset{&data_ptr[start], elems_per_node};

  /* Use qsort on the region */
  dump_shm();
  DBGH << "Workset.begin() = " << (void*)&data_ptr[start]
       << " elems per node = " << elems_per_node << std::endl;

  for (size_t i = 0; i < elems_per_node; i++) {
    for (size_t j = 1; j < elems_per_node; j++) {
      if (data_ptr[j - 1] > data_ptr[j]) {
	auto temp = data_ptr[j - 1];
        data_ptr[j - 1] = data_ptr[j];
        data_ptr[j] = temp;
      }
    }    
  }
  
  // data_ptr[0] = 1000;
  // std::sort(workset.begin(), workset.end(), std::greater<uint64_t>{});
  dump_shm();
  
  ASSERT_VALID;
  
  /* Signal ready */
  shm.value()->header.done[id] = 1;

  DBGH << "Written done to location "
       << (void*)&shm.value()->header.done[id]
       << std::endl;

  ASSERT_VALID;
  
  std::cout << "Sorting completed at " << id << std::endl;
}

void merge_worker() {
  ASSERT_VALID;
  
  size_t iter[NODES_WORKER];

  for (size_t i = 0; i < NODES_WORKER; i++) {
    iter[i] = 0;
  }

  auto next_iter = [&iter](size_t node) {
    if (iter[node] < shm.value()->header.elems) {
      iter[node]++;
    }
  };

  auto done = [&iter]() -> bool {
    for (size_t i = 0; i < NODES_WORKER; i++) {
      if (iter[i] != shm.value()->header.elems/NODES_WORKER){
	return false;
      }
    }
    
    return true;
  };
  
  while (!done()) {
    vector<uint64_t> cur_elems(NODES_WORKER);

    uint64_t max_elem = 0;
    uint64_t max_idx;
    for (size_t i = 0; i < NODES_WORKER; i++) {
      if (iter[i] != shm.value()->header.elems/NODES_WORKER) {
	// std::cout << "offset = " << i*ELEMS + iter[i] << std::endl;
	auto elems_per_node = shm.value()->header.elems/(NODES_WORKER);
	auto elem = shm.value()->data[i*elems_per_node + iter[i]];
	if (elem >= max_elem) {
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
  std::cout << "&elems = " << &shm.value()->header.elems << std::endl;
  std::cout << "elems = " << shm.value() << std::endl;
  shm.value()->header.ready = 0;

  for (size_t i = 0; i < NODES_WORKER; i++)
    shm.value()->header.done[i] = 0;

  shm.value()->header.nodes = NODES_WORKER;

  dump_shm();

  std::memcpy(&shm.value()->header.canary, CANARY_VAL,
	      sizeof(CANARY_VAL));


  shm.value()->header.ready = 1;
  
  ivy.dump_shm_page(0);
}

void wait_for_workers() {
  /* Wait for every worker to complete */
  bool all_done = false;
  size_t seclap = 0;
  
  while (!all_done) {
    ASSERT_VALID;
    
    /* Wait for 100ms before checking */
    std::this_thread::sleep_for(std::chrono::seconds(10));

    all_done = true;

    for (size_t i = 0; i < NODES_WORKER; i++) {
      if (shm.value()->header.done[i] != 1) {
	std::cout << "node " << i << " done val = "
		  << (int)shm.value()->header.done[i] << std::endl;
	
	DBGH << "Checking location "
	     << (void*)(&shm.value()->header.done[i])
	     << " for id " << i
	     << std::endl;
	/* At least one node is not ready yet, wait for it */
	all_done = false;
	break;
      }
    }

    seclap += 1;
    std::cout << seclap
	      << " seconds elapsed waiting for workers" << std::endl;
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
    sort_worker(id-1);
  }
  
  std::cout << "All done" << std::endl;

  using namespace std::chrono_literals;

  dump_shm();
  
  // std::this_thread::sleep_for(24h);
  while (1);
}
