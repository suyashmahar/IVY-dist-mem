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

pair<string, size_t> parse_addr(string addr) {
  string hostname_str, port_str;
  
  hostname_str = addr.substr(0, addr.find(":"));
  port_str = addr.substr(addr.find(":")+1, addr.length()-1);

  DBGH << "Got hostname = " << hostname_str
       << " and port = " << port_str
       << std::endl;

  if (hostname_str.empty())
    IVY_ERROR("Unable to parse hostname");
  
  if (port_str.empty())
    IVY_ERROR("Unable to parse port");

  uint64_t port_num = std::stoul(port_str.c_str(), nullptr, 10);
  if (port_num < 1 || port_num > 65536) {
    IVY_ERROR("Invalid port number " + port_str);
  }

  DBGH << "Returning " << port_num << std::endl;
  
  return {hostname_str, port_num};
}

RpcServer::RpcServer(vector<string> nodes, size_t myId)
  : nodes(nodes), myId(myId) {
  
  IVY_ASSERT(myId < nodes.size(), "myID greater than number of nodes");

  string addr = nodes[myId];
  
  DBGH << "Trying to translate " << addr << std::endl;
  
  this->hostname = addr.substr(0, addr.find(":"));
  string port_str = addr.substr(addr.find(":")+1, addr.length()-1);

  DBGH << "Got hostname = '" << hostname
       << "' and port = '" << port_str << "'"
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

  size_t client_iter = 0;
  for (string client_name : this->nodes) {
    auto [ node_name, node_port ] = parse_addr(client_name);

    auto c = std::make_unique<rpc::client>(node_name, node_port);
    this->clients.push_back(std::move(c));
	
    client_iter++;
  }

  auto ping_func = [] (string buf) { return "pong";};
  this->recv_funcs.push_back(std::make_pair("ping", ping_func));
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

  return {};
}

res_t<string> RpcServer::call(size_t nodeId, string name, string buf) {
  DBGH << "Calling function " << name << " on node " << nodeId
       << " with buffer " << buf << std::endl;
  res_t<string> result
    = {this->clients[nodeId]->call(name, buf).as<string>(), {}};

  DBGH << "Call complete" << std::endl;
  std::flush(std::cerr);
  std::flush(std::cout);
  return result;
}
