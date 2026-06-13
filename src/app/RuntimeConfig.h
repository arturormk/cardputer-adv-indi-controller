#pragma once

#include <stddef.h>
#include <stdint.h>

namespace app {

constexpr size_t kSsidSize = 33;
constexpr size_t kPasswordSize = 65;
constexpr size_t kHostSize = 64;

struct RuntimeConfig {
  char ssid[kSsidSize]{};
  char password[kPasswordSize]{};
  char host[kHostSize]{};
  uint16_t port = 7624;
};

bool validHost(const char* host);
bool validPort(uint16_t port);
bool validWifi(const RuntimeConfig& config);
bool validServer(const RuntimeConfig& config);

}  // namespace app

