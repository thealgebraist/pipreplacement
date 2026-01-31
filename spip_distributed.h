#pragma once
#include "spip_utils.h"

void init_queue_db(const Config& cfg);
void run_master(const Config& cfg, const std::vector<std::string>& args);
void run_worker(Config& cfg);
