// -*- mode: c++; c-basic-offset: 2; -*-

/**
 * @file   pagefaultlatency.cpp
 * @date   May 30, 2021
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
#include <stdexcept>
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
using std::chrono::high_resolution_clock;
using namespace std::chrono;  

optional<uint64_t*> shm;

int main(int argc, char *argv[]) {
  std::string cfg_fname = "";
  uint64_t id;
  
  if (argc != 3) {
    std::cout << "USAGE: " << argv[0] <<  " <path to config file> <id>"
	      << std::endl;
    exit(1);
  } else {
    cfg_fname = std::string(argv[1]);
    id = std::stoul(argv[2], nullptr, 10);
    
    std::cout << "Config file " << cfg_fname << std::endl;
    std::cout << "Id = " << id << std::endl;
  }

  Ivy ivy(cfg_fname, id);

  size_t region_sz = 2 * libivy::Ivy::PAGE_SZ;
  
  auto [shm_, err_] = ivy.get_shm();

  if (err_.has_value())
    throw std::runtime_error("SHM failed");
  else
    shm = reinterpret_cast<uint64_t*>(shm_);

  if (id == 0) std::memset(shm.value(), 0, region_sz);

  if (id == 1) {
    auto timeStart = high_resolution_clock::now();
    
    /* Fault the pages if this is not the manager node */
    std::cout << "Reading the first 8 bytes "
	      << shm.value()[0] << std::endl;

    auto timeEnd = high_resolution_clock::now();

    auto rd_flt_dur = duration_cast<milliseconds>(timeEnd - timeStart);

    timeStart = high_resolution_clock::now();
    shm.value()[0] = 1024;
    timeEnd = high_resolution_clock::now();

    auto wr_flt_dur = duration_cast<milliseconds>(timeEnd - timeStart);

    std::cout << "Time to read fault: "
	      << rd_flt_dur.count() << std::endl;
    std::cout << "Time to write fault: "
	      << wr_flt_dur.count() << std::endl;
  }

  while (1);
}
