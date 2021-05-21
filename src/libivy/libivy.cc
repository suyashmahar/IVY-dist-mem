// -*- mode: c++; c-basic-offset: 2; -*-

/**
 * @file   libivy.cpp
 * @date   May 13, 2021
 * @brief  Brief description here
 */

#include "common.hh"
#include "error.hh"
#include "libivy.hh"

#include <filesystem>
#include <fstream>

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


/** userfaultd doesn't have a libc wrapper, see USERFAULTD(2) */
int userfaultfd(int flags) { return syscall(SYS_userfaultfd, flags); }

Ivy::Ivy(std::string cfg_f, idx_t id) {
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

mres_t Ivy::reg_fault_hdlr() {
  DBGH << "Registering handler" << std::endl;

  /* Open a userfaultd filedescriptor */
  if ((this->fd = userfaultfd(O_NONBLOCK)) == -1) {
    return {"userfaultfd failed"};
  }

  /* Check if the kernel supports the read/POLLIN protocol */
  struct uffdio_api uapi = {};
  uapi.api = UFFD_API;
  if (ioctl(fd, UFFDIO_API, &uapi)) {
    return {"ioctl(fd, UFFDIO_API, ...) failed"};
  }

  if (uapi.api != UFFD_API) {
    return {"unexepcted UFFD api version"};
  }

  /* Start a fault monitoring thread */
  /* start a thread that will fault... */
  pthread_t thread = {0};
  this->fault_hdlr_live.lock();
  if (pthread_create(&thread, NULL, this->pg_fault_hdlr, this)) {
    return {"pthread_create failed"};
  }

  /* Wait for fault handlers to go live */
  this->fault_hdlr_live.lock();

  return {};
}

void* Ivy::pg_fault_hdlr(void *args) {
  auto *ivy = reinterpret_cast<Ivy*>(args);
  
  /* Signal libivy to continue */
  ivy->fault_hdlr_live.unlock();
  
  struct pollfd evt = {};
  evt.fd = ivy->fd;
  evt.events = POLLIN;

  auto err = [&]() {
    if (ivy->fd) close(ivy->fd);
    DBGE << "Cannot continue, exiting with code 1..." << std::endl;
    exit(1);
  };

  while (1) {
    int pollval = poll(&evt, 1, 10);

    switch (pollval) {
    case -1:
      perror("poll/userfaultfd");
      continue;
    case 0:
      continue;
    case 1:
      break;
    default:
      DBGH << "unexpected poll result" << std::endl;
      exit(1);
    }
      
    /* unexpected poll events */
    if (evt.revents & POLLERR) {
      DBGH << "++ POLLERR" << std::endl;
      err();
    } else if (evt.revents & POLLHUP) {
      DBGH << "++ POLLHUP" << std::endl;
      err();
    }
    struct uffd_msg fault_msg = {0};
    if (read(ivy->fd, &fault_msg, sizeof(fault_msg)) != sizeof(fault_msg)) {
      perror("ioctl_userfaultfd read");
      DBGH << "++ read failed" << std::endl;
      err();
    }

    ivy->rpcserver->call(1, "ping", "");
    
    char *addr = (char *)fault_msg.arg.pagefault.address;

    if (fault_msg.event & UFFD_EVENT_PAGEFAULT) {
      DBGH << "Got page fault at " << (void*)(addr) << std::endl;
      /* TODO: handle the page here */

      /* Check if this is a write fault */
      if (fault_msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WRITE) {
	DBGH << "Write fault" << std::endl;
	ivy->wr_fault_hdlr(addr);
      } else {
	DBGH << "Read fault" << std::endl;
	ivy->rd_fault_hdlr(addr);
      }
      std::this_thread::sleep_for(1000ms);
    }
  }
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

  DBGH << "userfaultd's fd = " << fd << std::endl;
  
  struct uffdio_register reg = {};
  
  reg.mode        = UFFDIO_REGISTER_MODE_MISSING;
  reg.range       = {};
  reg.range.start = (long) start;
  reg.range.len   = bytes;

  int ioctl_ret;
  if ((ioctl_ret = ioctl(fd, UFFDIO_REGISTER,  &reg))) {
    switch (ioctl_ret) {
    case EBUSY:
      DBGH << "EBUSY" << std::endl;
      break;
    case EFAULT:
      DBGH << "EFAULT" << std::endl;
      break;
    case EINVAL:
      DBGH << "EINVAL" << std::endl;
      break;
    }
    
    return {"ioctl(fd, UFFDIO_REGISTER,  &reg): " + PSTR()};
  }
  if (reg.ioctls != UFFD_API_RANGE_IOCTLS) {
    return {"UFFD_API_RANGE_IOCTLS: " + PSTR()};
  }
  
  return {};
} 

mres_t Ivy::req_manager(void_ptr addr, IvyAccessType access) { return {}; };
mres_t Ivy::ack_manager(void_ptr addr, IvyAccessType access) { return {}; };
mres_t Ivy::invalidate(void_ptr addr, vector<string> nodes) { return {}; };


