// -*- mode: c++; c-basic-offset: 2; -*-

/**
 * @file   rpcserver.cc
 * @date   May 17, 2021
 * @brief  Brief description here
 */

#include "error.hh"
#include "../common.hh"
#include "rpcserver.hh"
#include "rpc/rpc_error.h"

#include <future>
#include <thread>

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

string ping(string buf) {
  return "pong";
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

  uint64_t port_num = std::stoul(port_str.c_str(), nullptr, 10);
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
  
  this->recv_funcs.push_back(std::make_pair("ping", ping));
}

mres_t RpcServer::start_recv() {
  DBGH << "Starting RPC server for " << this->hostname
       << ":" << this->port << std::endl;
  
  for (size_t i = 0; i < recv_funcs.size(); i++) {
    this->server->bind(recv_funcs[i].first, recv_funcs[i].second);
  }

  // this->server->bind("ping", [](string arg) -> string {return "pong";});

  this->server->run();
  
  return {};
}

mres_t RpcServer::start_serving() {
  DBGH << "Starting RPC server thread asynchronously" << std::endl;

  auto bg_fun = [&] () { this->start_recv(); };
  std::thread(bg_fun).detach();

  return {};
}

res_t<string> RpcServer::call(size_t nodeId, string name, string buf) {
  DBGH << "Calling function " << name << " on node " << nodeId
       << " with buffer " << buf << " addr = " << this->nodes[nodeId]
       << std::endl;

  res_t<string> ret_val;
  
  try {
    auto msg = this->clients[nodeId]->call(name, buf);
    DBGH << "Message = " << msg.get() << std::endl;
    auto result = msg.as<string>();

    DBGH << "result = " << result << std::endl;
  
    ret_val = {result, {}};

    DBGH << "Call complete" << std::endl;
  } catch (std::exception &e) {
    DBGH << "RPC failed" << std::endl;
    return {"", "RPC failed"};
  }
  
  return ret_val;
}

void
RpcServer::register_recv_funcs(vector<pair<string, rpc_recv_f>> lst) {
  for (auto elem : lst) {
    this->server->bind(elem.first, elem.second);
  }
}
