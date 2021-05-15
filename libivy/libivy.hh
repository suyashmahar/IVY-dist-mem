// -*- mode: c++; c-basic-offset: 2; -*-

/**
 * @file   libivy.hpp
 * @date   May 13, 2021
 * @brief  Brief description here
 */

#ifndef IVY_HEADER_LIBIVY_IVY_H__
#define IVY_HEADER_LIBIVY_IVY_H__

#include <string>
#include <optional>
#include <variant>

#include "json.hpp"

using std::optional;
using std::pair;
using std::string;
using std::variant;
using std::vector;

class Ivy {
public:
  using idx_t = uint64_t;
  using err_t = std::string;

  template <typename T>
  using res_t = pair<T, optional<err_t>>;

  using void_ptr = void*;
  
private:
  using json = nlohmann::json;

  idx_t id;
  string addr;
  json cfg;
  
  vector<string> nodes;
  uint64_t manager_id;

  string NODES_KEY = "nodes";
  string MANAGER_ID_KEY = "manager_id";

public:  
  Ivy(std::string cfg_f, idx_t id);
  ~Ivy();


  res_t<void_ptr> get_shm();
  res_t<std::monostate> drop_shm(void_ptr region);
  res_t<bool> is_manager();

  res_t<bool> ca_va();
};

template <typename T>
static inline bool ivy_chk(Ivy::res_t<T> val) {
  if (val.index() == 1)
    return true;
  
  return false;
}

#endif // IVY_HEADER_LIBIVY_IVY_H__
