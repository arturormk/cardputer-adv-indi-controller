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

bool isLeapYear(int year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

int daysInMonth(int year, int month) {
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return month == 2 && isLeapYear(year) ? 29 : days[month - 1];
}

}  // namespace

bool addUtcSeconds(const char* utc, uint32_t seconds, char* output, size_t outputSize) {
  int year, month, day, hour, minute, second;
  if (!utc || !output || outputSize == 0 ||
      sscanf(utc, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6 ||
      month < 1 || month > 12 || day < 1 || day > daysInMonth(year, month) || hour < 0 ||
      hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) {
    return false;
  }
  uint32_t total = static_cast<uint32_t>(hour * 3600 + minute * 60 + second) + seconds;
  hour = static_cast<int>((total / 3600) % 24);
  minute = static_cast<int>((total / 60) % 60);
  second = static_cast<int>(total % 60);
  uint32_t days = total / 86400;
  while (days--) {
    if (++day <= daysInMonth(year, month)) continue;
    day = 1;
    if (++month <= 12) continue;
    month = 1;
    ++year;
  }
  return snprintf(output, outputSize, "%04d-%02d-%02dT%02d:%02d:%02d", year, month, day, hour,
                  minute, second) > 0;
}

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
