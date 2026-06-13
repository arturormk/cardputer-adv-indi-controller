#include "CameraModel.h"

#include <ctype.h>
#include <string.h>

namespace camera {
namespace {

bool containsIgnoreCase(const char* value, const char* needle) {
  if (!value || !needle) return false;
  const size_t length = strlen(needle);
  for (; *value; ++value) {
    size_t i = 0;
    while (i < length && value[i] &&
           tolower(static_cast<unsigned char>(value[i])) ==
               tolower(static_cast<unsigned char>(needle[i]))) {
      ++i;
    }
    if (i == length) return true;
  }
  return false;
}

}  // namespace

const indi::Property* Model::property(const char* name) const {
  return device_ ? cache_.findProperty(device_, name) : nullptr;
}

bool Model::isCamera() const {
  if (exposureProperty()) return true;
  const indi::Property* info = property("DRIVER_INFO");
  if (!info) return false;
  for (size_t i = 0; i < info->memberCount; ++i) {
    if (containsIgnoreCase(info->members[i].text, "ccd") ||
        containsIgnoreCase(info->members[i].text, "camera")) {
      return true;
    }
  }
  return false;
}

const indi::Property* Model::exposureProperty() const {
  return property("CCD_EXPOSURE");
}

const indi::Member* Model::exposureMember() const {
  const indi::Property* exposure = exposureProperty();
  if (!exposure || !exposure->memberCount) return nullptr;
  for (size_t i = 0; i < exposure->memberCount; ++i) {
    if (strcmp(exposure->members[i].name, "CCD_EXPOSURE_VALUE") == 0) return &exposure->members[i];
  }
  return exposure->memberCount == 1 ? &exposure->members[0] : nullptr;
}

const indi::Property* Model::isoProperty() const {
  if (const indi::Property* iso = property("CCD_ISO")) return iso;
  if (!device_) return nullptr;
  const size_t count = cache_.propertyCountForDevice(device_);
  for (size_t i = 0; i < count; ++i) {
    const indi::Property* candidate = cache_.propertyForDevice(device_, i);
    if (candidate->type == indi::PropertyType::Switch &&
        (containsIgnoreCase(candidate->name, "iso") || containsIgnoreCase(candidate->label, "iso"))) {
      return candidate;
    }
  }
  return nullptr;
}

const indi::SwitchOption* Model::activeIso() const {
  const indi::Property* iso = isoProperty();
  if (!iso) return nullptr;
  for (size_t i = 0; i < cache_.switchOptionCount(*iso); ++i) {
    const indi::SwitchOption* option = cache_.switchOption(*iso, i);
    if (option && option->active) return option;
  }
  return nullptr;
}

int Model::activeIsoIndex() const {
  const indi::Property* iso = isoProperty();
  if (!iso) return -1;
  for (size_t i = 0; i < cache_.switchOptionCount(*iso); ++i) {
    const indi::SwitchOption* option = cache_.switchOption(*iso, i);
    if (option && option->active) return static_cast<int>(i);
  }
  return -1;
}

size_t Model::isoCount() const {
  const indi::Property* iso = isoProperty();
  return iso ? cache_.switchOptionCount(*iso) : 0;
}

const indi::SwitchOption* Model::iso(size_t index) const {
  const indi::Property* propertyValue = isoProperty();
  return propertyValue ? cache_.switchOption(*propertyValue, index) : nullptr;
}

}  // namespace camera
