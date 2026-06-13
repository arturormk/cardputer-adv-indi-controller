#pragma once

#include "IndiTypes.h"

namespace indi {

class PropertyCache {
 public:
  void clear();
  Property* upsertProperty(const char* device, const char* name, PropertyType type);
  Property* findProperty(const char* device, const char* name);
  const Property* findProperty(const char* device, const char* name) const;
  Member* upsertMember(Property& property, const char* name);
  void upsertLargeSwitchOption(Property& property, const char* name, const char* label, bool active);
  size_t switchOptionCount(const Property& property) const;
  const SwitchOption* switchOption(const Property& property, size_t index) const;
  void remove(const char* device, const char* name);

  size_t deviceCount() const { return deviceCount_; }
  size_t propertyCount() const { return propertyCount_; }
  const Device& device(size_t index) const { return devices_[index]; }
  const Property& property(size_t index) const { return properties_[index]; }
  size_t propertyCountForDevice(const char* device) const;
  const Property* propertyForDevice(const char* device, size_t index) const;
  bool overflowed() const { return overflowed_; }

 private:
  Device* upsertDevice(const char* name);
  void recountDevices();

  Device devices_[kMaxDevices]{};
  Property properties_[kMaxProperties]{};
  LargeSwitchProperty largeSwitchProperties_[kMaxLargeSwitchProperties]{};
  size_t deviceCount_ = 0;
  size_t propertyCount_ = 0;
  bool overflowed_ = false;
};

}  // namespace indi
