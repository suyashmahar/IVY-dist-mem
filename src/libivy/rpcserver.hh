// -*- mode: c++; c-basic-offset: 2; -*-

/**
 * @file   rpcserver.hh
 * @date   May 17, 2021
 * @brief  Brief description here
 */


#ifndef IVY_HEADER_LIBIVY_RPCSERVER_H__
#define IVY_HEADER_LIBIVY_RPCSERVER_H__

#include "common.hh"

#include <string>
#include <vector>
#include <functional>

#include "rpc/server.h"
#include "rpc/client.h"

namespace libivy {
  using std::pair;
  using std::string;
  using std::vector;
  using std::unique_ptr;

  using rpc_recv_f = std::function<void(string)>;
  using rpc_send_f = std::function<mres_t(string)>;
  
  class RpcServer {
  private:
    string hostname;
    uint16_t port;

    unique_ptr<rpc::server> server;
    unique_ptr<rpc::client> client;
    
    vector<pair<string, rpc_recv_f>> recv_funcs;
    vector<pair<string, rpc_send_f>> send_funcs;

    mres_t start_recv();
    mres_t start_send();    
  public:
    RpcServer(string addr);
    
    void register_recv_funcs(vector<pair<string, rpc_recv_f>>);
    void register_send_funcs(vector<pair<string, rpc_recv_f>>);

    res_t<string> call(string name, string buf);

    mres_t start_serving(string addr);
  };
}

#endif // IVY_HEADER_LIBIVY_RPCSERVER_H__

