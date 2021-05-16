// -*- mode: c++; c-basic-offset: 2; -*-

/**
 * @file   libivy.hpp
 * @date   May 13, 2021
 * @brief  Brief description here
 */

#ifndef IVY_HEADER_LIBIVY_IVY_H__
#define IVY_HEADER_LIBIVY_IVY_H__

#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

#include "json.hpp"

using std::optional;
using std::pair;
using std::string;
using std::variant;
using std::vector;

using void_ptr = void *;

class Ivy {
  /* Public types and aliases */
public:
  using idx_t = uint64_t;
  using err_t = std::string;

  template <typename T>
  using res_t = pair<T, optional<err_t>>;
  using mres_t = optional<err_t>;

  using bytes_t = uint64_t;

  /* Private variables */
private:
  using json = nlohmann::json;

  idx_t id;
  string addr;
  json cfg;
  
  vector<string> nodes;
  uint64_t manager_id;

  void_ptr mem;

  string NODES_KEY = "nodes";
  string MANAGER_ID_KEY = "manager_id";

  int fd;

  enum AccessType {
    RD = 0,
    WR = 1,
    RW = 2
  };

  /* Public interface */
public:
  static constexpr bytes_t PAGE_SZ = 4096; // bytes
  
  Ivy(std::string cfg_f, idx_t id);
  ~Ivy();

  res_t<void_ptr> get_shm();
  mres_t drop_shm(void_ptr region);
  res_t<bool> is_manager();

  res_t<bool> ca_va();

  /* Private methods */
private:
  /** @brief Registers fault handler for this->mem */
  res_t<std::monostate> reg_fault_hdlr();

  /** @brief Handles the page faults for the memory region */
  static void_ptr pg_fault_hdlr(void *args);

  /** @brief Creates a memory region for sharing */
  res_t<void_ptr> create_mem_region(size_t bytes);

  /** @brief Changes access rights to a region */
  mres_t set_access(void_ptr addr, size_t pg_cnt, AccessType access);

  /** @brief Fetch a page from its owner directly to the destination */
  mres_t fetch_pg(void_ptr addr);

  /** @brief Service a read request for a page from the app */
  mres_t serv_rd_rq(void_ptr page_addr);
  
  /** @brief Service a write request for a page from the app */
  mres_t serv_wr_rq(void_ptr page_addr);
};

template <typename T>
static inline bool is_err(Ivy::res_t<T> val) {
  if (val.index() == 1)
    return true;
  
  return false;
}

template <typename T>
static inline T unwrap(Ivy::res_t<T> val) {
  if (val.second.has_value())
    throw std::runtime_error("Unwrap on error");
  return val.first;
}

#endif // IVY_HEADER_LIBIVY_IVY_H__
