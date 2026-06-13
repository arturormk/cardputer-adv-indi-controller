#pragma once

#include "indi/IndiPropertyCache.h"

namespace camera {

class Model {
 public:
  Model(const indi::PropertyCache& cache, const char* device) : cache_(cache), device_(device) {}

  bool isCamera() const;
  const indi::Property* property(const char* name) const;
  const indi::Property* exposureProperty() const;
  const indi::Member* exposureMember() const;
  const indi::Property* isoProperty() const;
  const indi::SwitchOption* activeIso() const;
  int activeIsoIndex() const;
  size_t isoCount() const;
  const indi::SwitchOption* iso(size_t index) const;

 private:
  const indi::PropertyCache& cache_;
  const char* device_;
};

}  // namespace camera
