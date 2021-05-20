// -*- mode: c++; c-basic-offset: 2; -*-

/**
 * @file   rpcserver.cc
 * @date   May 17, 2021
 * @brief  Brief description here
 */

#include "error.hh"
#include "../common.hh"
#include "rpcserver.hh"

#include <future>

using namespace libivy;

RpcServer::RpcServer(string addr) {
  this->hostname = addr.substr(0, addr.find("::"));
  string port_str = addr.substr(addr.find("::"), addr.length());

  DBGH << "Got hostname = " << hostname
       << " and port = " << port
       << std::endl;

  if (hostname.empty())
    IVY_ERROR("Unable to parse hostname");
  
  if (port_str.empty())
    IVY_ERROR("Unable to parse port");

  uint64_t port_num = std::stoul(port_str.c_str(), nullptr, 16);
  if (port_num < 1 || port_num > 65536) {
    IVY_ERROR("Invalid port number " + port_str);
  }

  this->port = port_num;

  this->server = std::make_unique<rpc::server>(this->port);
  this->client = std::make_unique<rpc::client>(this->hostname, this->port);
}

mres_t RpcServer::start_recv() {
  for (auto func : recv_funcs) {
    this->server->bind(func.first, func.second);
  }

  this->server->run();
  
  return {};
}

mres_t RpcServer::start_serving(string addr) {
  std::async(std::launch::async, [&]() {this->start_recv();});
}

res_t<string> RpcServer::call(string name, string buf) {
  return {this->client->call(name, buf).as<string>(), {}};
}
