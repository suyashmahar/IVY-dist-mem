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

  for (int i = 0; i < NODES; i++) {
    iter[i] = 0;
  }

  auto next_iter = [&iter](size_t node) {
    if (iter[node] < shm.value()->header.elems) {
      iter[node]++;
    }
  };

  auto done = [&iter]() -> bool {
    for (int i = 0; i < NODES; i++) {
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
    for (int i = 0; i < NODES; i++) {
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
    shm.value()->data[in_iter++] = in_num;
  }

  std::cout << "Read " << in_iter << " elems " << std::endl;
  
  shm.value()->header.elems = elems;
  shm.value()->header.ready = false;

  for (int i = 0; i < NODES; i++)
    shm.value()->header.done[i] = 0;

  shm.value()->header.nodes = NODES;

  dump_shm();

  shm.value()->header.ready = true;

  /* Create n parallel processes */
  for (int i = 0; i < NODES; i++) {
    std::thread t(sort_worker, i);
    t.detach();
  }

  std::cout << "All children spawned" << std::endl;

  /* Wait for every worker to complete */
  bool all_done = false;
  while (!all_done) {
    /* Wait for 100ms before checking */
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    all_done = true;

    for (int i = 0; i < NODES; i++) {
      if (shm.value()->header.done[i] != 1) {
	std::cout << "id " << i << " is not done yet" << std::endl;
	/* At least one node is not ready yet, wait for it */
	all_done = false;
	break;
      }
    }
  }

  std::cout << "All done" << std::endl;
  merge_worker();
}
