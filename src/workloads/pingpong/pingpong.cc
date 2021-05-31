// -*- mode: c++; c-basic-offset: 2; -*-

/**
 * @file   pingpong.cc
 * @date   May 26, 2021
 * @brief  Brief description here
 */

#include "common.hh"
#include "libivy.hh"

#include <iostream>
#include <thread>
#include <chrono>

bool should_send = false;

using namespace libivy;

constexpr size_t ITERATIONS = 5;

int main(int argc, char *argv[]) {
  using namespace std::chrono_literals;

  std::string config_fname;
  size_t id;
  if (argc != 3) {
    std::cout << "Usage: " << argv[0]
	      << " <path to config file>"
	      << " <nodeId>"
	      << std::endl;
    exit(1);
  } else {
    config_fname = std::string(argv[1]);
    id = std::strtoul(argv[2], nullptr, 10);
    if (id > 2) {
      std::cout << "Wrong node ID" << std::endl;
      exit(1);
    }
  }

  Ivy ivy(config_fname, id);
  auto [shm, err] = ivy.get_shm();
  if (err.has_value()) throw std::runtime_error(err.value());
  
  auto arr_ul = reinterpret_cast<uint64_t*>(shm);

  auto [is_manager, im_err] = ivy.is_manager();
  assert(!im_err.has_value() && "Manager lookup failed");
  
  if (is_manager) {
    arr_ul[0] = 0;
  } else {
    auto start_tm = std::chrono::high_resolution_clock::now();
    
    while (arr_ul[0] < ITERATIONS) {
      /* Busy wait on the value */
      while (arr_ul[0]%2 != id-1);

      arr_ul[0]++;
      
      std::cout << "Counter updated to "
		<< arr_ul[0] << std::endl;
    }
    
    auto end_tm = std::chrono::high_resolution_clock::now();

    auto dur = end_tm - start_tm;

    std::cout << "Seconds elapsed = "
	      << dur.count() << std::endl;
  }
  
  while (1);
}

