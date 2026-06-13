#pragma once

#include "RuntimeConfig.h"

namespace app {

class ConfigStore {
 public:
  bool load(RuntimeConfig& config);
  bool save(const RuntimeConfig& config);
  bool clear();
};

}  // namespace app

