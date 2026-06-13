#include "IndiWriter.h"

#include <stdio.h>
#include <string.h>

namespace indi {
namespace {

class Buffer {
 public:
  Buffer(char* output, size_t capacity) : output_(output), capacity_(capacity) {
    if (capacity_) output_[0] = '\0';
  }

  bool append(const char* text) {
    while (*text) {
      if (!append(*text++)) return false;
    }
    return true;
  }

  bool appendEscaped(const char* text) {
    while (*text) {
      const char* entity = nullptr;
      switch (*text) {
        case '&': entity = "&amp;"; break;
        case '<': entity = "&lt;"; break;
        case '>': entity = "&gt;"; break;
        case '"': entity = "&quot;"; break;
        case '\'': entity = "&apos;"; break;
      }
      if (entity ? !append(entity) : !append(*text)) return false;
      ++text;
    }
    return true;
  }

  size_t size() const { return valid_ ? length_ : 0; }

 private:
  bool append(char value) {
    if (!valid_ || length_ + 1 >= capacity_) {
      valid_ = false;
      if (capacity_) output_[0] = '\0';
      return false;
    }
    output_[length_++] = value;
    output_[length_] = '\0';
    return true;
  }

  char* output_;
  size_t capacity_;
  size_t length_ = 0;
  bool valid_ = true;
};

}  // namespace

size_t Writer::buildSwitchVector(char* output, size_t capacity, const Property& property,
                                 const char* activeMember) {
  Buffer buffer(output, capacity);
  if (!buffer.append("<newSwitchVector device=\"") || !buffer.appendEscaped(property.device) ||
      !buffer.append("\" name=\"") || !buffer.appendEscaped(property.name) ||
      !buffer.append("\">")) {
    return 0;
  }
  for (size_t i = 0; i < property.memberCount; ++i) {
    const Member& member = property.members[i];
    const bool active = activeMember && strcmp(member.name, activeMember) == 0;
    if (!buffer.append("<oneSwitch name=\"") || !buffer.appendEscaped(member.name) ||
        !buffer.append("\">") || !buffer.append(active ? "On" : "Off") ||
        !buffer.append("</oneSwitch>")) {
      return 0;
    }
  }
  if (!buffer.append("</newSwitchVector>\n")) return 0;
  return buffer.size();
}

size_t Writer::buildNumberVector(char* output, size_t capacity, const Property& property,
                                 const char* memberName, double value) {
  const NumberValue values[] = {{memberName, value}};
  return buildNumberVector(output, capacity, property, values, 1);
}

size_t Writer::buildNumberVector(char* output, size_t capacity, const Property& property,
                                 const NumberValue* values, size_t valueCount) {
  Buffer buffer(output, capacity);
  if (valueCount && !values) return 0;
  if (!buffer.append("<newNumberVector device=\"") || !buffer.appendEscaped(property.device) ||
      !buffer.append("\" name=\"") || !buffer.appendEscaped(property.name) ||
      !buffer.append("\">")) {
    return 0;
  }
  for (size_t i = 0; i < valueCount; ++i) {
    char number[32];
    snprintf(number, sizeof(number), "%.10g", values[i].value);
    if (!values[i].memberName || !buffer.append("<oneNumber name=\"") ||
        !buffer.appendEscaped(values[i].memberName) || !buffer.append("\">") ||
        !buffer.append(number) || !buffer.append("</oneNumber>")) {
      return 0;
    }
  }
  if (!buffer.append("</newNumberVector>\n")) return 0;
  return buffer.size();
}

size_t Writer::buildTextVector(char* output, size_t capacity, const Property& property,
                               const TextValue* values, size_t valueCount) {
  Buffer buffer(output, capacity);
  if (valueCount && !values) return 0;
  if (!buffer.append("<newTextVector device=\"") || !buffer.appendEscaped(property.device) ||
      !buffer.append("\" name=\"") || !buffer.appendEscaped(property.name) ||
      !buffer.append("\">")) {
    return 0;
  }
  for (size_t i = 0; i < valueCount; ++i) {
    if (!values[i].memberName || !values[i].value || !buffer.append("<oneText name=\"") ||
        !buffer.appendEscaped(values[i].memberName) || !buffer.append("\">") ||
        !buffer.appendEscaped(values[i].value) || !buffer.append("</oneText>")) {
      return 0;
    }
  }
  if (!buffer.append("</newTextVector>\n")) return 0;
  return buffer.size();
}

size_t Writer::buildSwitchMember(char* output, size_t capacity, const Property& property,
                                 const char* memberName, bool active) {
  Buffer buffer(output, capacity);
  if (!buffer.append("<newSwitchVector device=\"") || !buffer.appendEscaped(property.device) ||
      !buffer.append("\" name=\"") || !buffer.appendEscaped(property.name) ||
      !buffer.append("\"><oneSwitch name=\"") || !buffer.appendEscaped(memberName) ||
      !buffer.append("\">") || !buffer.append(active ? "On" : "Off") ||
      !buffer.append("</oneSwitch></newSwitchVector>\n")) {
    return 0;
  }
  return buffer.size();
}

}  // namespace indi
