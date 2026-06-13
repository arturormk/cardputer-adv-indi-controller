#include "RuntimeConfig.h"

#include <string.h>

namespace app {

bool validHost(const char* host) {
  if (!host || !*host || strlen(host) >= kHostSize) return false;
  for (const char* c = host; *c; ++c) {
    if (static_cast<unsigned char>(*c) <= ' ') return false;
  }
  return true;
}

bool validPort(uint16_t port) {
  return port != 0;
}

bool validWifi(const RuntimeConfig& config) {
  return *config.ssid && strlen(config.ssid) < kSsidSize &&
         strlen(config.password) < kPasswordSize;
}

bool validServer(const RuntimeConfig& config) {
  return validHost(config.host) && validPort(config.port);
}

}  // namespace app

