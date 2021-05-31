// -*- mode: c++; c-basic-offset: 2; -*-

/**
 * @file   common.hh
 * @date   May 16, 2021
 * @brief  Brief description here
 */

#ifndef IVY_HEADER_LIBIVY_COMMON_H__
#define IVY_HEADER_LIBIVY_COMMON_H__

#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <iostream>
#include <iomanip>

// Do magic! Creates a unique name using the line number
#define IVY_LINE_NAME( prefix ) IVY_JOIN( prefix, __LINE__ )
#define IVY_JOIN( symbol1, symbol2 ) IVY_DO_JOIN( symbol1, symbol2 )
#define IVY_DO_JOIN( symbol1, symbol2 ) symbol1##symbol2


#define ivyguard(mtx)				\
  std::lock_guard<std::mutex>			\
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
} // namespace libivy

static inline std::string to_hex(const char *arr, size_t len) {
  std::ostringstream ret;

  size_t last_len = 0;
  for (size_t i = 0; i < len; ++i) {
    std::ostringstream temp;
    uint32_t elem = static_cast<uint8_t>(arr[i]);
    
    temp << std::hex << std::setfill('0') << std::setw(2)
	 << std::uppercase << elem;
    
    ret << temp.str();
    last_len = ret.str().length();
  }
  
  return ret.str();
}

static inline char *from_hex(const std::string& s) {
  auto ret = new char[s.size()];

  for (size_t i = 0; i < s.size(); i+=2) {
    char cur_slice_raw[3] = {s[i], s[i+1], 0};
    std::string cur_slice = std::string(cur_slice_raw);

    ret[i/2] = std::stoul(cur_slice, nullptr, 16);
  }
  
  return ret;
}

static inline void dump_from_hex(const std::string &s) {
  return;
  
  std::cerr << std::endl;
  std::cerr << "Output : " << std::endl;

  char hr_form[17];
  std::memset(&hr_form, '.', 16);
  hr_form[16] = 0;
  
  for (size_t i = 0; i < s.length()/2; i+=2) {
    if (i % 32 == 0 && i != 0) {
      std::cerr << "  " << std::string(hr_form) << std::endl;
      std::memset(&hr_form, '.', 16);
      hr_form[16] = 0;
    }
    
    if (i % 16 == 0 && i % 32 != 0 && i != 0) std::cerr << "    ";

    char raw_seq[3] = {s[i], s[i+1], 0};
    char raw_char = std::stoul(std::string(raw_seq), nullptr, 16);

    if (raw_char >= ' ' && raw_char <= '~')
      hr_form[(i/2)%16] = raw_char;

    std::cerr << s[i] << s[i+1] << " ";
  }

  std::cerr << std::endl;
}

#endif // IVY_HEADER_LIBIVY_COMMON_H__
