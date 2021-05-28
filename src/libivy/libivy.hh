// -*- mode: c++; c-basic-offset: 2; -*-

/**
 * @file   libivy.hpp
 * @date   May 13, 2021
 * @brief  Brief description here
 */

#ifndef IVY_HEADER_LIBIVY_IVY_H__
#define IVY_HEADER_LIBIVY_IVY_H__

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>
#include <mutex>

#include "common.hh"
#include "../common.hh"
#include "ivypagetbl.hh"
#include "json.hpp"
#include "rpcserver.hh"

#include <signal.h>


namespace libivy {   
  using void_ptr = void *;
  using byte_ptr = uint8_t*;
  using libivy::RpcServer;
  using std::optional;
  using std::pair;
  using std::string;
  using std::variant;
  using std::vector;
  using std::unique_ptr;
  using std::mutex;

  constexpr char* FAIL_STR = (char*)"No can't do";
  constexpr size_t PG_SZ = 4096;
  
  class Ivy {
    /* Private variables */
  private:
    using json = nlohmann::json;

    idx_t id;
    string addr;
    json cfg;
    std::unique_ptr<RpcServer> rpcserver;
  
    vector<string> nodes;
    uint64_t manager_id;
    size_t region_sz; // bytes
    void_ptr region;
    mutex fault_hdlr_live;
    void_ptr base_addr;

    void_ptr mem;

    string NODES_KEY = "nodes";
    string MANAGER_ID_KEY = "manager_id";
    string REGION_SZ_KEY = "region_sz";
    string BASE_ADDR = "base_addr";

    string GET_OWNER = "get_owner";
    string FETCH_PG_RD = "fetch_pg_rd";
    string FETCH_PG_RW = "fetch_pg_rw";

    int fd;

    unique_ptr<libivy::IvyPageTable> pg_tbl;

    /* Public interface */
  public:
    static constexpr bytes_t PAGE_SZ = 4096; // bytes
  
    Ivy(std::string cfg_f, idx_t id);
    ~Ivy();

    res_t<void_ptr> get_shm();
    mres_t drop_shm(void_ptr region);
    res_t<bool> is_manager();

    res_t<bool> ca_va();
    void lock_all();
    void request_lock(void_ptr addr, size_t bytes);
    void dump_shm_page(size_t page_num);
    /* Private methods */
  private:
    
    
    /** @brief Check the permission of a memory location */
    IvyAccessType read_mem_perm(void_ptr addr);
    
    /** @brief Registers fault handler for this->mem */
    mres_t reg_fault_hdlr();

    /** @brief Handles the page faults for the memory region */
    static void sigaction_hdlr(int sig, siginfo_t *info, void * uctx);

    /** @brief Register a range of address with pg fault hdlr */
    mres_t reg_addr_range(void *start, size_t bytes);
  
    /** @brief Handles the page faults for the memory region */
    mres_t rd_fault_hdlr(void_ptr addr);
  
    /** @brief Handles the page faults for the memory region */
    mres_t wr_fault_hdlr(void_ptr addr);

    /** @brief Creates a memory region for sharing */
    res_t<void_ptr> create_mem_region(size_t bytes);

    /** @brief Changes access rights to a region */
    mres_t set_access(void_ptr addr, size_t pg_cnt,
		      IvyAccessType access);

    /** @brief Fetch a page from its owner directly to the destination */
    mres_t
    fetch_pg(size_t node, void_ptr addr, IvyAccessType accessType);

    /** @brief Service a read request for a page from the app */
    res_t<string> serv_rd_rq(void_ptr page_addr, idx_t node);
  
    /** @brief Service a write request for a page from the app */
    res_t<string> serv_wr_rq(void_ptr page_addr, idx_t node);

    /** @brief Check if the address is managed by the current node */
    bool is_owner(void_ptr pg_addr);
    
    /** @brief Get the owner node id from a page address */
    string get_owner_str(string pg_addr);
    
    /** @brief Get the owner node id from a page address */
    size_t get_owner(void_ptr pg_addr);

    /** @brief Ask manager for access to a page, returns owner */
    res_t<size_t> req_manager(void_ptr addr, IvyAccessType access);
  
    /** @brief Acknowledge manager for access to a page */
    mres_t ack_manager(void_ptr addr, IvyAccessType access);

    /** @brief Invalidates the page on every node (runs on manager) */
    mres_t invalidate(void_ptr addr, vector<size_t> nodes);

    /** @brief Read a page from memory and convert it to a string */
    string read_page(void_ptr addr);

    /* Adapter functions for RPC */

    /** @brief Adapts \ref serv_rd_rq */
    string serv_rd_rq_adapter(string in);
    
    /** @brief Adapts \ref serv_wr_rq */
    string serv_wr_rq_adapter(string in);
  };

  template <typename T>
  static inline bool is_err(res_t<T> val) {
    if (val.index() == 1)
      return true;
  
    return false;
  }

  template <typename T>
  static inline T unwrap(res_t<T> val) {
    if (val.second.has_value())
      throw std::runtime_error("Unwrap on error");
    return val.first;
  }
  
}

#endif // IVY_HEADER_LIBIVY_IVY_H__
