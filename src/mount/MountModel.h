#pragma once

#include "indi/IndiPropertyCache.h"

namespace mount {

class Model {
 public:
  Model(const indi::PropertyCache& cache, const char* device) : cache_(cache), device_(device) {}

  bool isMount() const;
  const indi::Property* property(const char* name) const;
  const indi::Member* member(const char* propertyName, const char* memberName) const;
  const indi::Member* activeMember(const char* propertyName) const;
  size_t slewRateCount() const;
  const indi::Member* slewRate(size_t index) const;
  int activeSlewRateIndex() const;

 private:
  const indi::PropertyCache& cache_;
  const char* device_;
};

}  // namespace mount

