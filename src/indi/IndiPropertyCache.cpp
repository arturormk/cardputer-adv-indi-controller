#include "IndiPropertyCache.h"

#include <string.h>

namespace indi {
namespace {

template <size_t N>
void copyBounded(char (&destination)[N], const char* source) {
  if (!source) source = "";
  strncpy(destination, source, N - 1);
  destination[N - 1] = '\0';
}

bool isControlPriority(const char* name) {
  static const char* priority[] = {
      "CONNECTION",          "TELESCOPE_INFO",       "EQUATORIAL_EOD_COORD",
      "HORIZONTAL_COORD",    "TELESCOPE_TRACK_MODE", "TELESCOPE_TRACK_STATE",
      "CELESTRON_TRACK_MODE", "TELESCOPE_MOTION_NS", "TELESCOPE_MOTION_WE",
      "TELESCOPE_SLEW_RATE", "TELESCOPE_ABORT_MOTION", "TIME_UTC",
      "GEOGRAPHIC_COORD",
      "CCD_EXPOSURE",       "CCD_ISO",               "CCD_GAIN",
      "CCD_CONTROLS",       "DRIVER_INFO",
  };
  for (const char* value : priority) {
    if (strcmp(name, value) == 0) return true;
  }
  return false;
}

bool isLargeSwitchProperty(const char* name) {
  return strcmp(name, "CCD_ISO") == 0;
}

}  // namespace

void PropertyCache::clear() {
  memset(devices_, 0, sizeof(devices_));
  memset(properties_, 0, sizeof(properties_));
  memset(largeSwitchProperties_, 0, sizeof(largeSwitchProperties_));
  deviceCount_ = 0;
  propertyCount_ = 0;
  overflowed_ = false;
}

Device* PropertyCache::upsertDevice(const char* name) {
  for (size_t i = 0; i < deviceCount_; ++i) {
    if (strcmp(devices_[i].name, name) == 0) return &devices_[i];
  }
  if (deviceCount_ >= kMaxDevices) {
    overflowed_ = true;
    return nullptr;
  }
  Device& device = devices_[deviceCount_++];
  copyBounded(device.name, name);
  return &device;
}

Property* PropertyCache::upsertProperty(const char* device, const char* name,
                                        PropertyType type) {
  if (!device || !name || !*device || !*name) return nullptr;
  if (Property* existing = findProperty(device, name)) {
    if (type != PropertyType::Unknown) existing->type = type;
    return existing;
  }
  if (propertyCount_ >= kMaxProperties) {
    overflowed_ = true;
    if (!isControlPriority(name)) return nullptr;
    if (!upsertDevice(device)) return nullptr;
    size_t replacement = propertyCount_;
    while (replacement > 0) {
      --replacement;
      if (!isControlPriority(properties_[replacement].name)) break;
    }
    if (isControlPriority(properties_[replacement].name)) return nullptr;
    properties_[replacement] = {};
    copyBounded(properties_[replacement].device, device);
    copyBounded(properties_[replacement].name, name);
    properties_[replacement].type = type;
    properties_[replacement].defined = true;
    recountDevices();
    return &properties_[replacement];
  }
  if (!upsertDevice(device)) return nullptr;
  Property& property = properties_[propertyCount_++];
  property = {};
  copyBounded(property.device, device);
  copyBounded(property.name, name);
  property.type = type;
  property.defined = true;
  recountDevices();
  return &property;
}

Property* PropertyCache::findProperty(const char* device, const char* name) {
  for (size_t i = 0; i < propertyCount_; ++i) {
    if (strcmp(properties_[i].device, device) == 0 &&
        strcmp(properties_[i].name, name) == 0)
      return &properties_[i];
  }
  return nullptr;
}

const Property* PropertyCache::findProperty(const char* device, const char* name) const {
  return const_cast<PropertyCache*>(this)->findProperty(device, name);
}

size_t PropertyCache::propertyCountForDevice(const char* device) const {
  size_t count = 0;
  for (size_t i = 0; i < propertyCount_; ++i) {
    if (strcmp(properties_[i].device, device) == 0) ++count;
  }
  return count;
}

const Property* PropertyCache::propertyForDevice(const char* device, size_t index) const {
  for (size_t i = 0; i < propertyCount_; ++i) {
    if (strcmp(properties_[i].device, device) == 0 && index-- == 0) return &properties_[i];
  }
  return nullptr;
}

Member* PropertyCache::upsertMember(Property& property, const char* name) {
  for (size_t i = 0; i < property.memberCount; ++i) {
    if (strcmp(property.members[i].name, name) == 0) return &property.members[i];
  }
  if (property.memberCount >= kMaxMembersPerProperty) {
    if (!isLargeSwitchProperty(property.name)) overflowed_ = true;
    return nullptr;
  }
  Member& member = property.members[property.memberCount++];
  member = {};
  copyBounded(member.name, name);
  return &member;
}

void PropertyCache::upsertLargeSwitchOption(Property& property, const char* name, const char* label,
                                            bool active) {
  if (property.type != PropertyType::Switch || !isLargeSwitchProperty(property.name)) return;
  LargeSwitchProperty* large = nullptr;
  for (auto& candidate : largeSwitchProperties_) {
    if (strcmp(candidate.device, property.device) == 0 &&
        strcmp(candidate.name, property.name) == 0) {
      large = &candidate;
      break;
    }
    if (!large && !*candidate.name) large = &candidate;
  }
  if (!large) {
    overflowed_ = true;
    return;
  }
  if (!*large->name) {
    copyBounded(large->device, property.device);
    copyBounded(large->name, property.name);
  }
  SwitchOption* option = nullptr;
  for (size_t i = 0; i < large->optionCount; ++i) {
    if (strcmp(large->options[i].name, name) == 0) {
      option = &large->options[i];
      break;
    }
  }
  if (!option) {
    if (large->optionCount >= kMaxLargeSwitchOptions) {
      overflowed_ = true;
      return;
    }
    option = &large->options[large->optionCount++];
    copyBounded(option->name, name);
  }
  if (label && *label) copyBounded(option->label, label);
  option->active = active;
}

size_t PropertyCache::switchOptionCount(const Property& property) const {
  for (const auto& large : largeSwitchProperties_) {
    if (strcmp(large.device, property.device) == 0 && strcmp(large.name, property.name) == 0) {
      return large.optionCount;
    }
  }
  return property.memberCount;
}

const SwitchOption* PropertyCache::switchOption(const Property& property, size_t index) const {
  for (const auto& large : largeSwitchProperties_) {
    if (strcmp(large.device, property.device) == 0 && strcmp(large.name, property.name) == 0) {
      return index < large.optionCount ? &large.options[index] : nullptr;
    }
  }
  if (index >= property.memberCount) return nullptr;
  static SwitchOption option;
  copyBounded(option.name, property.members[index].name);
  copyBounded(option.label, property.members[index].label);
  option.active = property.members[index].active;
  return &option;
}

void PropertyCache::remove(const char* device, const char* name) {
  for (size_t i = 0; i < propertyCount_;) {
    const bool deviceMatch = strcmp(properties_[i].device, device) == 0;
    const bool nameMatch = !name || !*name || strcmp(properties_[i].name, name) == 0;
    if (deviceMatch && nameMatch) {
      properties_[i] = properties_[--propertyCount_];
    } else {
      ++i;
    }
  }
  for (auto& large : largeSwitchProperties_) {
    if (strcmp(large.device, device) == 0 &&
        (!name || !*name || strcmp(large.name, name) == 0)) {
      large = {};
    }
  }
  recountDevices();
}

void PropertyCache::recountDevices() {
  for (size_t i = 0; i < deviceCount_; ++i) devices_[i].propertyCount = 0;
  for (size_t p = 0; p < propertyCount_; ++p) {
    for (size_t d = 0; d < deviceCount_; ++d) {
      if (strcmp(properties_[p].device, devices_[d].name) == 0) {
        ++devices_[d].propertyCount;
        break;
      }
    }
  }
  for (size_t i = 0; i < deviceCount_;) {
    if (devices_[i].propertyCount == 0) {
      devices_[i] = devices_[--deviceCount_];
    } else {
      ++i;
    }
  }
}

}  // namespace indi
