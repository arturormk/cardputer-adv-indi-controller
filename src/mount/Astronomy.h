#pragma once

namespace mount {

bool equatorialToHorizontal(double raHours, double decDegrees, double latitudeDegrees,
                            double longitudeDegrees, const char* utc, double& altitudeDegrees,
                            double& azimuthDegrees);

}  // namespace mount

