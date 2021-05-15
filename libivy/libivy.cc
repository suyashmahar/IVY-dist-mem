// -*- mode: c++; c-basic-offset: 2; -*-

/**
 * @file   libivy.cpp
 * @date   May 13, 2021
 * @brief  Brief description here
 */

#include "error.hh"
#include "libivy.hh"

#include <filesystem>
#include <fstream>

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
}

Ivy::~Ivy() {}

Ivy::res_t<Ivy::void_ptr> Ivy::get_shm() { return nullptr; }
Ivy::res_t<std::monostate> Ivy::drop_shm(void_ptr region) { return {}; }
Ivy::res_t<bool> Ivy::is_manager() { return true; }
Ivy::res_t<bool> Ivy::ca_va() { return true; }

