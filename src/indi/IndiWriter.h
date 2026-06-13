#pragma once

#include <stddef.h>

#include "IndiTypes.h"

namespace indi {

struct NumberValue {
  const char* memberName;
  double value;
};

struct TextValue {
  const char* memberName;
  const char* value;
};

class Writer {
 public:
  static size_t buildSwitchVector(char* output, size_t capacity, const Property& property,
                                  const char* activeMember);
  static size_t buildSwitchMember(char* output, size_t capacity, const Property& property,
                                  const char* memberName, bool active);
  static size_t buildNumberVector(char* output, size_t capacity, const Property& property,
                                  const char* memberName, double value);
  static size_t buildNumberVector(char* output, size_t capacity, const Property& property,
                                  const NumberValue* values, size_t valueCount);
  static size_t buildTextVector(char* output, size_t capacity, const Property& property,
                                const TextValue* values, size_t valueCount);
};

}  // namespace indi
