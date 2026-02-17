#pragma once
#include "config.hpp"
#include "cron_store.hpp"
#include "skills_loader.hpp"

namespace minidragon {
int cmd_status();
int cmd_doctor();
int cmd_sessions(const std::string& subcmd, const std::string& arg);
} // namespace minidragon
