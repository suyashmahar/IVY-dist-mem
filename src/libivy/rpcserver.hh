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

  using rpc_recv_f = std::function<string(string)>;
  using rpc_send_f = std::function<mres_t(string)>;
  
  class RpcServer {
  private:
    string hostname;
    uint16_t port;

    unique_ptr<rpc::server> server;
    vector<unique_ptr<rpc::client>> clients;
    
    vector<pair<string, rpc_recv_f>> recv_funcs;

    vector<string> nodes;
    size_t myId;

    mres_t start_recv();
    mres_t start_send();    
  public:
    RpcServer(vector<string> nodes, size_t myId);
    ~RpcServer();
    /** @brief Register */
    void register_recv_funcs(vector<pair<string, rpc_recv_f>>);

    /** @brief Call a remote function */
    res_t<string> call(size_t nodeId, string name, string buf);

    /** @brief Same as \ref call , but blocks until the success */
    res_t<string> call_blocking(size_t nodeId, string name, string buf);

    mres_t start_serving();

    string ca_va() { return "ca va"; }
  };
}

#endif // IVY_HEADER_LIBIVY_RPCSERVER_H__

