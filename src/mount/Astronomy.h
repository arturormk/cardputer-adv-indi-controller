#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mount {

bool equatorialToHorizontal(double raHours, double decDegrees, double latitudeDegrees,
                            double longitudeDegrees, const char* utc, double& altitudeDegrees,
                            double& azimuthDegrees);
bool addUtcSeconds(const char* utc, uint32_t seconds, char* output, size_t outputSize);

}  // namespace mount
