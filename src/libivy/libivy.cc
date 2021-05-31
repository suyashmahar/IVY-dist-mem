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
#include <thread>
#include <unistd.h>

using namespace libivy;
using namespace std::placeholders;

using std::optional;
using std::pair;
using std::string;
using std::variant;
using std::vector;


static Ivy *ivy_static_obj = nullptr;

Ivy::Ivy(std::string cfg_f, idx_t id) {
  
  DBGH << "Created Ivy " << (void_ptr)this << std::endl;
  
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
    
    auto base_addr_str = this->cfg[BASE_ADDR].get<string>();
    auto base_addr_ul = std::stoul(base_addr_str, nullptr, 16);
    this->base_addr = reinterpret_cast<void_ptr>(base_addr_ul);
    
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

  this->pg_tbl = std::make_unique<IvyPageTable>();
  
  auto get_rd_page_f
    = [this](string in) -> string {
      DBGH << "Got call for get_rd_page_from_manager" << std::endl;
      
      string result = this->serv_rd_rq_adapter(in);
      DBGH << "Response to get_rd_page " << in << " -> size("
	   << result.length() << ")" << std::endl;

      return result;
    };
  auto get_wr_page_f
    = [this](string in) -> string {
      DBGH << "Got call for get_wr_page_from_manager" << std::endl;

      string result = this->serv_wr_rq_adapter(in);
      DBGH << "Response to get_wr_page " << in << " -> size("
	   << result.length() << ")" << std::endl;

      return result;
    };

  auto fetch_pg_adapter_f = [&](string in) -> string {
    return this->fetch_pg_adapter(in);
  };

  auto invalidate_adapter_f = [&](string in) -> string {
    DBGH << "Got call for invalidate: " << in << std::endl;

    return this->invalidate_adapter(in);
  };
  
  this->rpcserver->register_recv_funcs({
      {GET_RD_PAGE_FROM_MANAGER, get_rd_page_f},
      {GET_WR_PAGE_FROM_MANAGER, get_wr_page_f},
      {FETCH_PG, fetch_pg_adapter_f},
      {INVALIDATE_PG, invalidate_adapter_f},
    });

  this->rpcserver->start_serving();
}

Ivy::~Ivy() = default;

res_t<void_ptr> Ivy::get_shm() {
  void_ptr result = mmap(this->base_addr, this->region_sz,
			 PROT_READ | PROT_WRITE,
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
string Ivy::fetch_pg_adapter(string in) {
  DBGH << "Fetch pg adapter called with input " << in << std::endl;
  
  auto sep_pos = in.find(':');
  auto pg_addr = in.substr(0, sep_pos);
  auto perm = in.substr(sep_pos + 1, in.length() - 1);

  auto accessType = IvyAccessType::NONE;
  if (perm == "rd") {
    accessType = IvyAccessType::RD;
  } else if (perm == "none") {
    accessType = IvyAccessType::NONE;
  } else {
    IVY_ERROR("Unkown accessType for fetch_pg: " + perm);
  }


  auto addr_ul = std::stoul(pg_addr, nullptr, 10);
  auto addr_ptr = reinterpret_cast<void_ptr>(addr_ul);

  DBGH << "Address " << addr_ptr << std::endl;
  
  auto result = this->fetch_pg(addr_ptr, accessType);
  DBGH << "Response to fetch_pg " << in << " -> size(" << result.length()
       << ")" << std::endl;
  return result;
};

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
    auto addr = reinterpret_cast<void_ptr>(info->si_addr);

    auto perm = ivy_static_obj->read_mem_perm(addr);
    DBGH << "Page permission = " << perm << std::endl;
    
    if (uctx->uc_mcontext.gregs[REG_ERR] & 0x2) {
      DBGH << "Write fault" << std::endl;
      auto err = ivy_static_obj->wr_fault_hdlr(addr);

      if (err.has_value()) {
	DBGH << "Error: " << err.value() << std::endl;
	exit(1);
      }
      
    } else {
      DBGH << "Read fault" << std::endl;
      auto err = ivy_static_obj->rd_fault_hdlr(addr);
      
      if (err.has_value()) {
	DBGH << "Error: " << err.value() << std::endl;
	exit(1);
      }
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

mres_t Ivy::set_access(void_ptr addr, size_t pg_cnt,
			    IvyAccessType access) {
  int prot = 0;
  string prot_str = "";
  
  switch (access) {
  case (IvyAccessType::RD):
    prot |= PROT_READ;
    prot_str = "read";
    break;
  case (IvyAccessType::WR):
    prot |= PROT_WRITE;
    prot_str = "write";
    break;
  case (IvyAccessType::RW):
    prot |= PROT_READ | PROT_WRITE;
    prot_str = "read and write";
    break;
  case (IvyAccessType::NONE):
    prot = 0;
    prot_str = "none";
    break;
  default:
    DBGH << "Unknown access type" << std::endl;
    return {"Unknown access type"};
  }

  auto addr_val = reinterpret_cast<size_t>(addr);
  auto addr_pg = pg_align(addr_val);
  auto addr_pg_ptr = reinterpret_cast<void_ptr>(addr_pg);

  DBGH << "mprotect(" << addr_pg_ptr << ", "
       << pg_cnt*Ivy::PAGE_SZ << ", "
       << prot_str << ")" << std::endl;

  
  int mp_res = mprotect(addr_pg_ptr, pg_cnt * Ivy::PAGE_SZ, prot);

  if (mp_res == -1) {
    DBGH << "mprotect() failed" << std::endl;
    return std::make_optional(PSTR());
  }

  return {};
}

string
Ivy::fetch_pg(void_ptr addr, IvyAccessType accessType) {
  auto addr_ul = reinterpret_cast<uint64_t>(addr);
  auto addr_pg = pg_align(addr_ul);
  auto addr_str = std::to_string(addr_ul);

  if (!unwrap(this->is_manager())) {
    DBGH << "Getting lock for addr " << P(addr_ul) << std::endl;
    this->pg_tbl->page_locks[addr_ul].lock();
    DBGH << "Lock address = " << P(&pg_tbl->page_locks[addr_ul])
	 << std::endl;
  }

  string req_name = "";

  /* Change this page to read only */
  this->set_access((void_ptr)addr_pg, 1, IvyAccessType::RD);
  
  auto mem_str = this->read_page((void_ptr)addr_pg);

  DBGH << "Receive size = " << mem_str.length() << std::endl;
  IVY_ASSERT(mem_str.length() == PAGE_SZ*2,
	     "Fetch page did not receive 4096 bytes");

  dump_from_hex(mem_str);
  
  this->set_access((void_ptr)addr_pg, 1, accessType);

  if (!unwrap(this->is_manager())) {
    this->pg_tbl->page_locks[addr_ul].unlock();
  }
  
  return mem_str;
}

res_t<string> Ivy::serv_rd_rq(void_ptr pg_addr, idx_t req_node) {
  uint64_t addr_val = pg_align(reinterpret_cast<uint64_t>(pg_addr));

  DBGH << "Getting lock for addr " << P(addr_val) << std::endl;
  ivyguard(this->pg_tbl->page_locks[addr_val]);
  DBGH << "Lock address = " << P(&pg_tbl->page_locks[addr_val])
       << std::endl;
    
  FUNC_DUMP;
  IVY_ASSERT(unwrap(this->is_manager()), "get rd on non manager node");
  IVY_ASSERT(this->pg_tbl, "Page table uninit");

  std::string page_contents = "";

  /* Serve read request can only be called on the manager node,
     manager would contact the owner and return the page to the
     callee */
  if (unwrap(this->is_manager())){
    DBGH << "Getting info lock for addr " << P(addr_val) << std::endl;
    ivyguard(this->pg_tbl->info_locks[addr_val]);

    this->pg_tbl->info[addr_val].copyset.insert(req_node);

    auto owner_node = this->pg_tbl->info[addr_val].owner;
    auto req = std::to_string(addr_val) + ":rd";

    DBGH << "Calling fetch_pg_adapter(" << req << ")" << std::endl;

    if (owner_node == 0) {
      /* If the owner is the manager, don't go through the HTTP
	 server */
      page_contents = this->fetch_pg_adapter(req);
    } else {
      auto [page_cnt_, err_]
	= this->rpcserver->call_blocking(owner_node, FETCH_PG, req);
      IVY_ASSERT(!err_.has_value(), "Blocking call failed");
      page_contents = page_cnt_;
    }
  } else {
    IVY_ERROR("Tried serving from non-manager node");
  }

  IVY_ASSERT(!page_contents.empty(), "Could not read the memory page");

  DBGH << "Returning page's content" << std::endl;
  
  return {page_contents, {}};
}

res_t<string> Ivy::serv_wr_rq(void_ptr pg_addr, idx_t req_node) {
  uint64_t addr_val = pg_align(reinterpret_cast<uint64_t>(pg_addr));
  
  DBGH << "Getting lock for addr " << P(addr_val) << std::endl;
  ivyguard(this->pg_tbl->page_locks[addr_val]);
  DBGH << "Lock address = " << P(&pg_tbl->page_locks[addr_val])
       << std::endl;
  
  DBGH << "Address = " << pg_addr << std::endl;
  IVY_ASSERT(unwrap(this->is_manager()), "get wr on non manager node");

  DBGH << "Addr_val = " << addr_val << std::endl;
  DBGH << "owner = " << this->pg_tbl->info[addr_val].owner
       << std::endl;

  IVY_ASSERT(this->pg_tbl, "Page table uninit");

  std::string page_contents = "";
  
  auto owner_node = this->pg_tbl->info[addr_val].owner;

  /* Similar to serv read req, serv write req can only be served from
     the manager node */
  if (unwrap(this->is_manager())) {
    DBGH << "Getting info lock for addr " << P(addr_val) << std::endl;
    ivyguard(this->pg_tbl->info_locks[addr_val]);

    vector<size_t> ivld_set;

    /* Remove the node requesting the page before sending out
       invalidations */
    this->pg_tbl->info[addr_val].copyset.erase(req_node);

    /* Convert the set to a vector */
    std::copy(this->pg_tbl->info[addr_val].copyset.begin(),
	      this->pg_tbl->info[addr_val].copyset.end(),
	      std::back_inserter(ivld_set));

    auto req = std::to_string(addr_val) + ":none";
    
    DBGH << "fetch_pg_adapter(" << req << ")" << std::endl;

    if (owner_node == 0) {
      /* Call the function directly if the manager is also the
	 owner */
      auto page_cnt_ = this->fetch_pg_adapter(req);
      page_contents = page_cnt_;
    } else if (owner_node == req_node) {
      page_contents = "";
    } else if (owner_node != 0) {
      auto [page_cnt_, err_]
	= this->rpcserver->call_blocking(owner_node, FETCH_PG, req);
      IVY_ASSERT(!err_.has_value(), "Reading page failed");
      page_contents = page_cnt_;
    } else {    
    }
    
    auto err = this->send_invalidations(pg_addr, ivld_set);

    this->pg_tbl->info[addr_val].copyset.clear();
    this->pg_tbl->info[addr_val].owner = req_node;

  } else {
    IVY_ERROR("Tried serving from non-manager node");
  }
  
  if (owner_node != req_node)
    IVY_ASSERT(!page_contents.empty(),
	       "Could not read the memory page");
  
  return {page_contents, {}};
}

mres_t Ivy::rd_fault_hdlr(void_ptr addr) {
  FUNC_DUMP;

  uint64_t addr_val = pg_align(reinterpret_cast<uint64_t>(addr));

  if (!unwrap(this->is_manager())) {
    DBGH << "Getting lock for addr " << P(addr_val) << std::endl;
    this->pg_tbl->page_locks[addr_val].lock();
    DBGH << "Lock address = " << P(&pg_tbl->page_locks[addr_val])
	 << std::endl;
  }

  IVY_ASSERT(this->pg_tbl, "Page table uninit");

  /* Ask the manager for the page, the manager will contact the
     correct owner */
  auto err = this->get_rd_page_from_mngr(addr);
  
  if (err.has_value())
    return {"Reading from manager failed:" + err.value()};
  
  this->pg_tbl->info[addr_val].access = IvyAccessType::RD;

  DBGH << "Read fault serviced " << std::endl;

  if (!unwrap(this->is_manager()))
    this->pg_tbl->page_locks[addr_val].unlock();

  return {};
}

mres_t Ivy::wr_fault_hdlr(void_ptr addr) {
  IVY_ASSERT(this->pg_tbl, "Page table uninit");

  uint64_t addr_val = pg_align(reinterpret_cast<uint64_t>(addr));
  
  if (!unwrap(this->is_manager())) {
    DBGH << "Getting lock for addr " << P(addr_val) << std::endl;
    this->pg_tbl->page_locks[addr_val].lock();
    DBGH << "Lock address = " << P(&pg_tbl->page_locks[addr_val])
	 << std::endl;
  }

  mres_t result = {};

  auto err = this->get_wr_page_from_mngr(addr);
  
  IVY_ASSERT(!err.has_value(), "Reading from manager failed");

  this->pg_tbl->info[addr_val].access = IvyAccessType::WR;

  DBGH << "Write fault serviced" << std::endl;

  if (!unwrap(this->is_manager()))
    this->pg_tbl->page_locks[addr_val].unlock();
  
  return result;
}

mres_t Ivy::reg_addr_range(void *start, size_t bytes) {
  IVY_ASSERT(this->fd != 0, "fd not initialized");

  if (-1 == mprotect(start, bytes, PROT_NONE)) {
    return {PSTR()};
  }
  return {};
}

mres_t Ivy::get_rd_page_from_mngr(void_ptr addr) {
  auto addr_aligned = pg_align(addr);
  auto addr_ul = reinterpret_cast<uint64_t>(addr_aligned);
  auto addr_str = std::to_string(addr_ul);

  string mem_str;

  string payload = ":" + std::to_string(this->id);
  
  if (unwrap(this->is_manager())) {
    /* Skip the HTTP server if I'm the manager */
    mem_str = this->serv_wr_rq_adapter(addr_str + payload);
  } else {
    /* Otherwise, call the manager node */
    auto [mem_str_, err_]
      = this->rpcserver->call_blocking(0, GET_RD_PAGE_FROM_MANAGER,
				       addr_str + payload);

    IVY_ASSERT(!err_.has_value(), "Fetching page from manager failed");
    mem_str = mem_str_;
  }
  
  IVY_ASSERT(mem_str.length() == 2*PAGE_SZ,
	     "Not enough bytes received from the manager"
	     + std::string(", expected ") + std::to_string(2*PAGE_SZ)
	     + std::string(", got ") + std::to_string(mem_str.length()));
  
  auto mem = from_hex(mem_str);

  /* Write the page to node's memory and set the correct permission */
  this->set_access(addr_aligned, 1, IvyAccessType::RW);
  std::memcpy(addr_aligned, mem, PAGE_SZ);
  this->set_access(addr_aligned, 1, IvyAccessType::RD);

  return {};
}

mres_t Ivy::get_wr_page_from_mngr(void_ptr addr) {
  auto addr_aligned = pg_align(addr);
  auto addr_ul = reinterpret_cast<uint64_t>(addr_aligned);
  auto addr_str = std::to_string(addr_ul);

  string mem_str;
  
  string payload = ":" + std::to_string(this->id);
  
  if (unwrap(this->is_manager())) {
    /* Skip the HTTP server if I'm the manager */
    mem_str = this->serv_wr_rq_adapter(addr_str + payload);
  } else {
    /* Otherwise, call the manager for the page */
    auto [mem_str_, err_]
      = this->rpcserver->call_blocking(0, GET_WR_PAGE_FROM_MANAGER,
				     addr_str + payload);
    IVY_ASSERT(!err_.has_value(), "Fetching page from manager failed");
    mem_str = mem_str_;
  }

  auto cur_perm = this->read_mem_perm(addr_aligned);
  
  /* Set the correct permission and copy the page to node's memory */
  this->set_access(addr_aligned, 1, IvyAccessType::RW);


  /* if this node already has read access to the page, no need to copy
     it to the memory */
  if (cur_perm != IvyAccessType::RD) {
    std::stringstream errmsg;
    errmsg << "Not enough bytes received from the manager, expected "
	   << 2*PAGE_SZ << ", got " << mem_str.length()
	   << std::endl;
    IVY_ASSERT(mem_str.length() == 2*PAGE_SZ, errmsg.str());
  
    auto mem = from_hex(mem_str);

    std::memcpy(addr_aligned, mem, PAGE_SZ);
  }

  return {};  
}


mres_t Ivy::send_invalidations(void_ptr addr, vector<size_t> nodes) {
  DBGH << "Sending out invalidations for addr " << addr << std::endl;

  for (auto node : nodes) {
    printf("Node = %ld\n", node);
    // DBGH << "Processing node " << node << std::endl;
    auto addr_ul = reinterpret_cast<uint64_t>(addr);
    DBGH << this->rpcserver->ca_va() << std::endl;
    auto [resp, err] = this->rpcserver->call(node, INVALIDATE_PG,
					    std::to_string(addr_ul));

    if (err.has_value()) {
      return err;
    } else if (resp != OK) {
      return {"Invalidation failed for node " + std::to_string(node)};
    } else {
      DBGH << "Invalidation OK" << std::endl;
    }
  }

  DBGH << "Invalidation complete" << std::endl;
  
  return {};
}

mres_t Ivy::invalidate(void_ptr addr) {
  return this->set_access(addr, 1, IvyAccessType::NONE);
}

string Ivy::invalidate_adapter(const string in) {
  const auto addr = in;
  const auto addr_ul = pg_align(std::stoul(addr, nullptr, 10));
  const auto addr_ptr = reinterpret_cast<void_ptr>(addr_ul);

  if (!unwrap(this->is_manager())) {
    DBGH << "Getting lock for addr " << P(addr_ul) << std::endl;
    this->pg_tbl->page_locks[addr_ul].lock();
    DBGH << "Lock address = " << P(&pg_tbl->page_locks[addr_ul])
	 << std::endl;
  }

  auto err = this->invalidate(addr_ptr);

  if (!unwrap(this->is_manager())) {
    this->pg_tbl->page_locks[addr_ul].unlock();
  }
  
  if (err.has_value())
    return NOT_OK;
  else
    return OK;
}

std::string Ivy::read_page(void_ptr addr) {
  auto aligned_addr = pg_align(addr);
  auto *page = reinterpret_cast<const char*>(aligned_addr);

  auto result = to_hex(page, PAGE_SZ);
  DBGH << "read_page result.size = " << result.size() << std::endl;
  IVY_ASSERT(result.size() == PAGE_SZ*2, "Reading memory failed");

  dump_from_hex(result);

  return result;
}

string Ivy::serv_rd_rq_adapter(string in) {
  auto sep = in.find(":");
  auto addr_str = in.substr(0, sep);
  auto req_node_str = in.substr(sep+1, in.size()-1);

  DBGH << "Got RD request for addr = " << addr_str << " from node "
       << req_node_str << std::endl;

  auto addr_ul = std::stoul(addr_str, nullptr, 10);
  auto addr_ptr = reinterpret_cast<void_ptr>(addr_ul);
  addr_ptr = pg_align(addr_ptr);
  
  auto req_node = std::stoul(req_node_str, nullptr, 10);
  
  auto [res, err] = this->serv_rd_rq(addr_ptr, req_node);
  if (err.has_value()) IVY_ERROR(err.value());
  return res;
}
    
string Ivy::serv_wr_rq_adapter(string in) {
  auto sep = in.find(":");
  auto addr_str = in.substr(0, sep);
  auto req_node_str = in.substr(sep+1, in.size()-1);

  DBGH << "Got WR request for addr = " << addr_str << " from node "
       << req_node_str << std::endl;


  auto addr_ul = std::stoul(addr_str, nullptr, 10);
  auto addr_ptr = reinterpret_cast<void_ptr>(addr_ul);
  DBGH << addr_str << " -> " << addr_ul << " -> " << addr_ptr
       << std::endl;
  addr_ptr = pg_align(addr_ptr);

  DBGH << addr_str << " -> " << addr_ul << " -> " << addr_ptr
       << std::endl;

  
  auto req_node = std::stoul(req_node_str, nullptr, 10);
  
  auto [res, err] = this->serv_wr_rq(addr_ptr, req_node);
  if (err.has_value()) IVY_ERROR(err.value());
  return res;  
}

void Ivy::dump_shm_page(size_t page_num) {
  auto mem_str = this->read_page(((byte_ptr)this->base_addr)
				 + page_num*PAGE_SZ);

  dump_from_hex(mem_str);
  
}
