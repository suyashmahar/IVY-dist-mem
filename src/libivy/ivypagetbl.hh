// -*- mode: c++; c-basic-offset: 2; -*-

/**
 * @file   ivypagetbl.hh
 * @date   May 16, 2021
 * @brief  Brief description here
 */

#ifndef IVY_HEADER_LIBIVY_IVYPAGETBL_H__
#define IVY_HEADER_LIBIVY_IVYPAGETBL_H__

#include <mutex>
#include <set>
#include <map>

#include "common.hh"

namespace libivy {
  using std::map;
  using std::mutex;
  using std::string;
  using std::set;
  
  class IvyPageTable {
  public:
    struct info_t {
      // /* Struct with mutex can't have copy constructor */
      // info_t (const info_t&) = delete;
      // info_t& operator=(const info_t&) = delete;
      
      set<string> copyset;
      IvyAccessType access;
    };
  
    using addr_t = uint64_t;
  
    map<addr_t, mutex> page_locks;
    map<addr_t, mutex> info_locks;
    map<addr_t, info_t> info;
  };

}
#endif // IVY_HEADER_LIBIVY_IVYPAGETBL_H__

