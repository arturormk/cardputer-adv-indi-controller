#pragma once

#include <stddef.h>

#include "IndiTypes.h"

namespace indi {

class Writer {
 public:
  static size_t buildSwitchVector(char* output, size_t capacity, const Property& property,
                                  const char* activeMember);
  static size_t buildSwitchMember(char* output, size_t capacity, const Property& property,
                                  const char* memberName, bool active);
  static size_t buildNumberVector(char* output, size_t capacity, const Property& property,
                                  const char* memberName, double value);
};

}  // namespace indi
