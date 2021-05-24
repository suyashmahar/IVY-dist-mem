// -*- mode: c++; c-basic-offset: 2; -*-

/**
 * @file   libivy.cpp
 * @date   May 13, 2021
 * @brief  Brief description here
 */

#include "common.hh"
#include "error.hh"
#include "libivy.hh"

#include <csignal>
#include <filesystem>
#include <fstream>
#include <memory>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace libivy;

using std::optional;
using std::pair;
using std::string;
using std::variant;
using std::vector;

static Ivy* ivy_static_obj = nullptr;

Ivy::Ivy(std::string cfg_f, idx_t id) {
  if (ivy_static_obj != nullptr){
    IVY_ERROR("An ivy object already exists");
  }
  ivy_static_obj = this;
  
  if (!std::filesystem::exists(cfg_f)) {
    IVY_ERROR("Config file not found.");
  }

  std::ifstream cfg_obj(cfg_f);

  /* Read the configuration file */
  cfg_obj >> this->cfg;

  try {
    this->nodes = this->cfg[NODES_KEY].get<vector<string>>();
    this->manager_id = this->cfg[MANAGER_ID_KEY].get<uint64_t>();
    this->region_sz = this->cfg[REGION_SZ_KEY].get<uint64_t>();
    
    DBGH << "Original region sz = " << this->region_sz << std::endl;    
    this->region_sz = pg_align(this->region_sz);

    DBGH << "New region sz = " << this->region_sz << std::endl;
  } catch (nlohmann::json::exception &e) {
    IVY_ERROR("Config file has wrong format.");
  }

  if (id >= this->nodes.size()) {
    IVY_ERROR("Node id cannot be greater than total number of nodes");
  }

  this->id = id;
  this->addr = this->nodes[id];
  this->rpcserver
    = std::make_unique<RpcServer>(this->nodes, this->id);
  
  auto err = this->reg_fault_hdlr();

  if (err.has_value()) {
    IVY_ERROR(PSTR());
  }

  this->rpcserver->start_serving();
}

Ivy::~Ivy() = default;

res_t<void_ptr> Ivy::get_shm() {
  void_ptr result = mmap(NULL, this->region_sz, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);

  if (result == MAP_FAILED) {
    return {nullptr, {"MAP_FAILED: " + PSTR()}};
  }

  DBGH << "Registering range from " << result << " + "
       << this->region_sz << std::endl;
  auto err = this->reg_addr_range(result, this->region_sz);
  if (err.has_value()) {
    DBGH << "reg_addr_range failed" << std::endl;
  }

  this->region = result;
  
  return {result, err};
}

mres_t Ivy::drop_shm(void_ptr region) { return {}; }

res_t<bool> Ivy::is_manager() {
  if (this->id == this->manager_id) {
    return {true, {}};
  } else {
    return {false, {}};
  }
}

res_t<bool> Ivy::ca_va() { return {true, {}}; }

IvyAccessType Ivy::read_mem_perm(void_ptr addr) {
  std::ifstream maps_f("/proc/" + std::to_string(getpid()) + "/maps");

  std::string line;
  while (std::getline(maps_f, line)) {
    std::stringstream token_stream(line);
    std::vector<std::string> tokens;
    
    std::string token;
    while (std::getline(token_stream, token, ' ')) {
      tokens.push_back(token);
    }

    if (tokens.size() > 2) {
      size_t hyp_pos = tokens[0].find("-");
      std::string start_str = tokens[0].substr(0, hyp_pos);
      std::string end_str = tokens[0].substr(hyp_pos+1, tokens[0].size()-1);

      auto start
	= reinterpret_cast<void_ptr>(std::stoul(start_str, nullptr, 16));
      auto end
	= reinterpret_cast<void_ptr>(std::stoul(end_str, nullptr, 16));
      
      std::string perm = tokens[1];

      if (addr >= start || addr < end) {
	if (perm[0] == 'r' && perm[1] == 'w')
	  return IvyAccessType::RW;
	else if (perm[0] == 'r')
	  return IvyAccessType::RD;
	else if (perm[1] == 'w')
	  return IvyAccessType::WR;
      }
    }
  }
}

void Ivy::sigaction_hdlr(int sig, siginfo_t *info, void *ctx_ptr) {
  auto uctx = reinterpret_cast<ucontext_t*>(ctx_ptr);
  
  DBGH << "Signal = " << (int)sig << std::endl;
  DBGH << "Signal = " << info->si_signo << std::endl;
  DBGH << "PID    = " << info->si_pid << std::endl;
  DBGH << "addr   = " << info->si_addr << std::endl;

  if (ivy_static_obj != nullptr) {
    auto perm = ivy_static_obj->read_mem_perm(info->si_addr);
    DBGH << "Page permission = " << perm << std::endl;
    if (uctx->uc_mcontext.gregs[REG_ERR] & 0x2) {
      DBGH << "Write fault" << std::endl;
      ivy_static_obj->wr_fault_hdlr(nullptr);
    } else {
      DBGH << "Write fault" << std::endl;
      ivy_static_obj->rd_fault_hdlr(nullptr);
    }
  }
}

mres_t Ivy::reg_fault_hdlr() {
  struct sigaction act {};

  act.sa_sigaction = &Ivy::sigaction_hdlr;
  sigfillset(&act.sa_mask);
  act.sa_flags = SA_RESTART | SA_SIGINFO;
  
  if (sigaction(SIGSEGV, &act, NULL) == -1) {
    DBGH << "sigaction failed: " << PSTR() << std::endl;
    exit(1);
  }

  return {};
}

res_t<void_ptr> Ivy::create_mem_region(size_t bytes) {
  void_ptr addr = mmap(nullptr, bytes,
		       PROT_EXEC | PROT_READ | PROT_READ,
		       MAP_SHARED | MAP_ANONYMOUS, 0, 0);

  if (addr == (void_ptr)-1) {
    return {nullptr, "mmap(2) failed"};
  }

  return {addr, {}};
}

mres_t Ivy::set_access(void_ptr addr, size_t pg_cnt,
			    IvyAccessType access) {
  int prot = PROT_EXEC;

  switch (access) {
  case (IvyAccessType::RD):
    prot |= PROT_READ;
    break;
  case (IvyAccessType::WR):
    prot |= PROT_WRITE;
    break;
  case (IvyAccessType::RW):
    prot |= PROT_READ | PROT_WRITE;
    break;
  default:
    return {"Unknown access type"};
  }
  
  int mp_res = mprotect(addr, pg_cnt * Ivy::PAGE_SZ, prot);

  if (mp_res == -1) {
    return std::make_optional(PSTR());
  }

  return {};
}

bool Ivy::is_owner(void_ptr pg_addr) {
  // TODO: Implement this
  return true;
}

mres_t Ivy::fetch_pg(void_ptr addr){
  throw std::runtime_error("Unimplemented");
}

mres_t Ivy::serv_rd_rq(void_ptr pg_addr) {
  
  throw std::runtime_error("Unimplemented");
}

mres_t Ivy::serv_wr_rq(void_ptr pg_addr) {
  throw std::runtime_error("Unimplemented");
}

mres_t Ivy::rd_fault_hdlr(void_ptr addr) {
  IVY_ASSERT(this->pg_tbl.has_value(), "Page table uninit");

  uint64_t addr_val = reinterpret_cast<uint64_t>(addr);
  
  pg_tbl.value()->page_locks[addr_val].lock();
  guard(pg_tbl.value()->page_locks[addr_val]);

  if (unwrap(this->is_manager())) {
    guard(this->pg_tbl.value()->info_locks[addr_val]);
    
    this->pg_tbl.value()->info[addr_val].copyset.insert(this->addr);
    this->fetch_pg(addr);

  } else {
    this->req_manager(addr, IvyAccessType::RD);
    this->fetch_pg(addr);
    this->ack_manager(addr, IvyAccessType::RD);
  }

  this->pg_tbl.value()->info[addr_val].access = IvyAccessType::RD;

  return {};
}

mres_t Ivy::wr_fault_hdlr(void_ptr addr) {
  return {};
  IVY_ASSERT(this->pg_tbl.has_value(), "Page table uninit");

  uint64_t addr_val = reinterpret_cast<uint64_t>(addr);

  pg_tbl.value()->page_locks[addr_val].lock();
  guard(pg_tbl.value()->page_locks[addr_val]);

  if (unwrap(this->is_manager())) {
    guard(this->pg_tbl.value()->info_locks[addr_val]);

    vector<string> ivld_set;
    std::copy(this->pg_tbl.value()->info[addr_val].copyset.begin(),
	      this->pg_tbl.value()->info[addr_val].copyset.end(),
	      std::back_inserter(ivld_set));
    
    this->invalidate(addr, ivld_set);

    this->pg_tbl.value()->info[addr_val].copyset.clear();
  } else {
    this->req_manager(addr, IvyAccessType::WR);
    this->fetch_pg(addr);
    this->ack_manager(addr, IvyAccessType::WR);
  }

  this->pg_tbl.value()->info[addr_val].access = IvyAccessType::WR;
  
  return {};
}

mres_t Ivy::reg_addr_range(void *start, size_t bytes) {
  IVY_ASSERT(this->fd != 0, "fd not initialized");

  if (-1 == mprotect(start, bytes, PROT_NONE)) {
    return {PSTR()};
  }
  return {};
} 

mres_t Ivy::req_manager(void_ptr addr, IvyAccessType access) { return {}; };
mres_t Ivy::ack_manager(void_ptr addr, IvyAccessType access) { return {}; };
mres_t Ivy::invalidate(void_ptr addr, vector<string> nodes) { return {}; };

void Ivy::request_lock(void_ptr addr, size_t bytes) {
  DBGH << "Locking range " << addr << " + "
       << bytes << std::endl;
  
  int mu_res = munmap(addr, bytes);
  if (mu_res == -1) {
    IVY_PERROR("munmap failed");
  }
  
  void_ptr result = mmap(addr, bytes, PROT_READ | PROT_WRITE,
			 MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);

  if (result == MAP_FAILED) {
    IVY_ERROR("mmap failed");
  }

  this->reg_addr_range(addr, bytes);

  DBGH << "locking done" << std::endl;
}
