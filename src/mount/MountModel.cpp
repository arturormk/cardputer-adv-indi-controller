#include "MountModel.h"

#include <string.h>

namespace mount {

const indi::Property* Model::property(const char* name) const {
  return device_ ? cache_.findProperty(device_, name) : nullptr;
}

bool Model::isMount() const {
  return property("EQUATORIAL_EOD_COORD") || property("TELESCOPE_MOTION_NS") ||
         property("TELESCOPE_MOTION_WE") || property("TELESCOPE_TRACK_STATE") ||
         property("TELESCOPE_INFO") || property("CELESTRON_TRACK_MODE");
}

const indi::Member* Model::member(const char* propertyName, const char* memberName) const {
  const indi::Property* value = property(propertyName);
  if (!value) return nullptr;
  for (size_t i = 0; i < value->memberCount; ++i) {
    if (strcmp(value->members[i].name, memberName) == 0) return &value->members[i];
  }
  return nullptr;
}

const indi::Member* Model::activeMember(const char* propertyName) const {
  const indi::Property* value = property(propertyName);
  if (!value) return nullptr;
  for (size_t i = 0; i < value->memberCount; ++i) {
    if (value->members[i].active) return &value->members[i];
  }
  return nullptr;
}

size_t Model::slewRateCount() const {
  const indi::Property* value = property("TELESCOPE_SLEW_RATE");
  return value ? value->memberCount : 0;
}

const indi::Member* Model::slewRate(size_t index) const {
  const indi::Property* value = property("TELESCOPE_SLEW_RATE");
  return value && index < value->memberCount ? &value->members[index] : nullptr;
}

int Model::activeSlewRateIndex() const {
  const indi::Property* value = property("TELESCOPE_SLEW_RATE");
  if (!value) return -1;
  for (size_t i = 0; i < value->memberCount; ++i) {
    if (value->members[i].active) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace mount
