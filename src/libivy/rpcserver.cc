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

  this->server = std::make_unique<httplib::Server>();

  size_t client_iter = 0;
  for (string client_name : this->nodes) {
    DBGH << "Creating client " << client_name << std::endl;

    auto c = std::make_unique<httplib::Client>(client_name.c_str());
    this->clients.push_back(std::move(c));
	
    client_iter++;
  }
  
  this->recv_funcs.push_back(std::make_pair("ping", ping));

  DBGH << "Created an RPCServer at " << (void*)this << std::endl;
}

mres_t RpcServer::start_recv() {
  DBGH << "Starting RPC server for " << this->hostname
       << ":" << this->port << std::endl;
  
  for (auto &recv_fun : this->recv_funcs) {
    DBGH << "Registering function " << recv_fun.first << std::endl;
    
    auto fun = [&](const auto &req, auto &res) {
      auto arg = std::string(req.body);
      
      auto &user_fun = recv_fun.second;

      auto val = user_fun(arg);

      // DBGH << "RPC(" << req.path << ") -> ("
	   // << to_hex(val.c_str(), val.length()) << ")" << std::endl;
      
      res.set_content(val.c_str(), "text/plain");
    };

    auto fun_name = recv_fun.first;
    
    this->server->Post(fun_name.c_str(), fun);
  }

  std::thread([&]() {
    this->server->listen(this->hostname.c_str(), this->port);
  }).detach();
  
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
    auto msg = this->clients[nodeId]->Post(name.c_str(),
					   buf.c_str(), "text/plain");

    if (!msg)
      throw std::runtime_error(std::to_string((int)msg.error()));

    
    DBGH << "Message = " << msg << std::endl;

    auto result = std::string(msg->body);

    // DBGH << "result = " << result << std::endl;
  
    ret_val = {result, {}};

    DBGH << "Call complete" << std::endl;
  } catch (std::exception &e) {
    DBGH << "RPC failed: " << e.what() << std::endl;
    return {"", "RPC failed"};
  }
  
  return ret_val;
}

res_t<string>
RpcServer::call_blocking(size_t nodeId, string name, string buf) {
  optional<err_t> err = "";
  string val;
  
  while (err.has_value()) {
    auto [res_, err_] = this->call(nodeId, name, buf);

    DBGH << "Sleeping for 1000ms" << std::endl;
    std::this_thread::sleep_for(1000ms);

    val = res_;
    err = err_;
  }

  return {val, {}};
}

void
RpcServer::register_recv_funcs(vector<pair<string, rpc_recv_f>> lst) {
  for (auto elem : lst) {
    this->recv_funcs.push_back({elem.first, elem.second});
  }
}


RpcServer::~RpcServer() {
  DBGH << "Destructor for rpcserver called" << std::endl;
}
