#include "Astronomy.h"

#include <math.h>
#include <stdio.h>

namespace mount {
namespace {

constexpr double kPi = 3.14159265358979323846;

double radians(double degrees) {
  return degrees * kPi / 180.0;
}

double degrees(double radiansValue) {
  return radiansValue * 180.0 / kPi;
}

double normalize360(double value) {
  value = fmod(value, 360.0);
  return value < 0 ? value + 360.0 : value;
}

}  // namespace

bool equatorialToHorizontal(double raHours, double decDegrees, double latitudeDegrees,
                            double longitudeDegrees, const char* utc, double& altitudeDegrees,
                            double& azimuthDegrees) {
  int year, month, day, hour, minute;
  double second;
  if (!utc || sscanf(utc, "%d-%d-%dT%d:%d:%lf", &year, &month, &day, &hour, &minute, &second) != 6)
    return false;
  if (month <= 2) {
    --year;
    month += 12;
  }
  const int century = year / 100;
  const int correction = 2 - century + century / 4;
  const double dayFraction = (hour + minute / 60.0 + second / 3600.0) / 24.0;
  const double julianDate = floor(365.25 * (year + 4716)) + floor(30.6001 * (month + 1)) +
                            day + dayFraction + correction - 1524.5;
  const double siderealDegrees =
      normalize360(280.46061837 + 360.98564736629 * (julianDate - 2451545.0) + longitudeDegrees);
  const double hourAngle = radians(normalize360(siderealDegrees - raHours * 15.0));
  const double declination = radians(decDegrees);
  const double latitude = radians(latitudeDegrees);
  const double altitude =
      asin(sin(declination) * sin(latitude) +
           cos(declination) * cos(latitude) * cos(hourAngle));
  const double azimuth =
      atan2(-sin(hourAngle) * cos(declination),
            sin(declination) * cos(latitude) -
                cos(declination) * sin(latitude) * cos(hourAngle));
  altitudeDegrees = degrees(altitude);
  azimuthDegrees = normalize360(degrees(azimuth));
  return true;
}

}  // namespace mount
