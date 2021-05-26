// -*- mode: c++; c-basic-offset: 2; -*-

/**
 * @file   common.hh
 * @date   May 16, 2021
 * @brief  Brief description here
 */

#ifndef IVY_HEADER_LIBIVY_COMMON_H__
#define IVY_HEADER_LIBIVY_COMMON_H__

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <iostream>

// Do magic! Creates a unique name using the line number
#define IVY_LINE_NAME( prefix ) IVY_JOIN( prefix, __LINE__ )
#define IVY_JOIN( symbol1, symbol2 ) IVY_DO_JOIN( symbol1, symbol2 )
#define IVY_DO_JOIN( symbol1, symbol2 ) symbol1##symbol2


#define guard(mtx)							\
  std::lock_guard<std::mutex>						\
  IVY_LINE_NAME(ivy_macro_lock_guard)((mtx))

template <typename T>
static inline constexpr T pg_align(T addr) {
  auto addr_uc = reinterpret_cast<uint64_t>(addr);
  size_t result = (addr_uc/4096)*4096;

  return reinterpret_cast<T>(result);
}

namespace libivy {
  using namespace std::chrono_literals;
  using std::optional;
  using std::pair;

  using idx_t = uint64_t;
  using err_t = std::string;

  template <typename T>
  using res_t = pair<T, optional<err_t>>;
  using mres_t = optional<err_t>;
  using bytes_t = uint64_t;

  enum IvyAccessType {
    RD   = 0,
    WR   = 1,
    RW   = 2,
    NONE = 3
  };
}

#endif // IVY_HEADER_LIBIVY_COMMON_H__
