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
  } catch (nlohmann::json::exception &e) {
    IVY_ERROR("Config file has wrong format.");
  }

  if (id >= this->nodes.size()) {
    IVY_ERROR("Node id cannot be greater than total number of nodes");
  }

  this->id = id;
  this->addr = this->nodes[id];
}

Ivy::~Ivy() = default;

Ivy::res_t<void_ptr> Ivy::get_shm() { return {nullptr, {}}; }
Ivy::mres_t Ivy::drop_shm(void_ptr region) { return {}; }

Ivy::res_t<bool> Ivy::is_manager() {
  if (this->id == this->manager_id) {
    return {true, {}};
  } else {
    return {false, {}};
  }
}

Ivy::res_t<bool> Ivy::ca_va() { return {true, {}}; }

Ivy::res_t<std::monostate> Ivy::reg_fault_hdlr() {
  DBGH << "Registering handler" << std::endl;

  auto err = [](string msg) -> res_t<std::monostate> {
    return {{}, msg};
  };
    
  /* Open a userfaultd filedescriptor */
  if ((fd = userfaultfd(O_NONBLOCK)) == -1) {
    return err("userfaultfd failed");
  }

  /* Check if the kernel supports the read/POLLIN protocol */
  struct uffdio_api uapi = {};
  uapi.api = UFFD_API;
  if (ioctl(fd, UFFDIO_API, &uapi)) {
    return err("ioctl(fd, UFFDIO_API, ...) failed");
  }

  if (uapi.api != UFFD_API) {
    return err("unexepcted UFFD api version");
  }

  /* Start a fault monitoring thread */
  /* start a thread that will fault... */
  pthread_t thread = {0};
  if (pthread_create(&thread, NULL, this->pg_fault_hdlr, this)) {
    return err("pthread_create failed");
  }

  return {};
}

void* Ivy::pg_fault_hdlr(void *args) {
  auto ivy = *reinterpret_cast<Ivy*>(args);

  DBGH << "Ca va? " << unwrap(ivy.ca_va()) << std::endl;
  
  struct pollfd evt = {};
  evt.fd = ivy.fd;
  evt.events = POLLIN;

  auto err = [&]() {
    if (ivy.fd) close(ivy.fd);
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
    if (read(ivy.fd, &fault_msg, sizeof(fault_msg)) != sizeof(fault_msg)) {
      perror("ioctl_userfaultfd read");
      DBGH << "++ read failed" << std::endl;
      err();
    }
    
    char *addr = (char *)fault_msg.arg.pagefault.address;

    if (fault_msg.event & UFFD_EVENT_PAGEFAULT) {
      DBGH << "Got page fault at " << (void*)(addr) << std::endl;
      /* TODO: handle the page here */
    }
  }
}

Ivy::res_t<void_ptr> Ivy::create_mem_region(size_t bytes) {
  void_ptr addr = mmap(nullptr, bytes,
		       PROT_EXEC | PROT_READ | PROT_READ,
		       MAP_SHARED | MAP_ANONYMOUS, 0, 0);

  if (addr == (void_ptr)-1) {
    return {nullptr, "mmap(2) failed"};
  }

  return {addr, {}};
}

Ivy::mres_t Ivy::set_access(void_ptr addr, size_t pg_cnt,
			    Ivy::AccessType access) {
  int prot = PROT_EXEC;

  switch (access) {
  case (Ivy::AccessType::RD):
    prot |= PROT_READ;
    break;
  case (Ivy::AccessType::WR):
    prot |= PROT_WRITE;
    break;
  case (Ivy::AccessType::RW):
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

Ivy::mres_t Ivy::fetch_pg(void_ptr addr){
  throw std::runtime_error("Unimplemented");
}

Ivy::mres_t Ivy::serv_rd_rq(void_ptr pg_addr) {
  throw std::runtime_error("Unimplemented");
}

Ivy::mres_t Ivy::serv_wr_rq(void_ptr pg_addr) {
  throw std::runtime_error("Unimplemented");
}

