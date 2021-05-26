// -*- mode: c++; c-basic-offset: 2; -*-

/**
 * @file   pingpong.cc
 * @date   May 26, 2021
 * @brief  Brief description here
 */

#include "httplib.h"
#include "common.hh"

#include <iostream>
#include <thread>
#include <chrono>

std::string hostnames[] = {"localhost", "localhost"};
size_t ports[] = {40000, 40001};

bool should_send = false;

int main(int argc, char *argv[]) {
  using namespace std::chrono_literals;
  
  if (argc != 2) {
    std::cout << "Usage: " << argv[0] << " <nodeId>" << std::endl;
    exit(1);
  }

  auto srv_nodeId = std::stoul(argv[1], nullptr, 10);

  auto srv_hostname = hostnames[srv_nodeId];
  auto srv_port = ports[srv_nodeId];

  auto cl_nodeId = (srv_nodeId + 1)%2;
  auto cl_port = ports[cl_nodeId];

  std::cout << "Server, hostname = " << srv_hostname
	    << ", port = " << srv_port << std::endl;
  std::cout << "Client port = " << cl_port << std::endl;

  httplib::Server srv;
  
  should_send = srv_nodeId == 0;
  
  srv.Get("/ping", [&](const httplib::Request &, httplib::Response &res){
    std::cout << "ping received" << std::endl;
    res.set_content("", "text/plain");
    should_send = true;
  });
  
  std::thread([&](){srv.listen(srv_hostname.c_str(), srv_port);}).detach();

  auto client_nm = "localhost:" + std::to_string(cl_port);
  httplib::Client cl(client_nm.c_str());
  
  while (true) {
    if (should_send) {
      bool done = false;

      while (!done) {
	try {
	  auto res = cl.Get("/ping");

	  if (!res) {
	    std::string err_val = std::to_string((int)res.error());
	    throw std::runtime_error(err_val);
	  } else {
	    done = true;
	    should_send = false;
	  }
	} catch (std::exception &e) {
	  std::cout << e.what() << std::endl;
	  std::cout << "Sleeping for 1s" << std::endl;
	  std::this_thread::sleep_for(1000ms);
	}
	
      }
    } else {
      std::cout << "should_send = false, sleeping for 1s" << std::endl;
      std::this_thread::sleep_for(1000ms);
    }
  }
}

