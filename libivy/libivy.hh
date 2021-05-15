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

using std::variant;

class Ivy {
  
private:
  using json = nlohmann::json;
  json cfg;

public:
  using idx_t = uint64_t;
  using err_t = std::string;

  template <typename T>
  using res_t = variant<T, err_t>;

  using void_ptr = void*;
  
  Ivy(std::string cfg_f, idx_t id);
  ~Ivy();

  res_t<void_ptr> get_shm();
  res_t<bool> drop_shp(void_ptr region);
};

#endif // IVY_HEADER_LIBIVY_IVY_H__
