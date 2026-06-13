#include "GnssState.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace gnss {
namespace {

template <size_t N>
void copyBounded(char (&destination)[N], const char* source) {
  strncpy(destination, source ? source : "", N - 1);
  destination[N - 1] = '\0';
}

template <size_t N>
void joinFields(char (&destination)[N], char* const* fields, size_t start, size_t count) {
  size_t length = 0;
  for (size_t i = start; i < count && length + 1 < N; ++i) {
    if (i > start && length + 1 < N) destination[length++] = ',';
    for (const char* value = fields[i]; *value && length + 1 < N; ++value) {
      destination[length++] = *value;
    }
  }
  destination[length] = '\0';
}

bool parseUnsigned(const char* value, unsigned& result) {
  if (!value || !*value) return false;
  char* end = nullptr;
  const unsigned long parsed = strtoul(value, &end, 10);
  if (!end || *end) return false;
  result = static_cast<unsigned>(parsed);
  return true;
}

bool parseDecimal(const char* value, double& result) {
  if (!value || !*value) return false;
  char* end = nullptr;
  result = strtod(value, &end);
  return end && !*end && isfinite(result);
}

bool parseCoordinate(const char* value, const char* hemisphere, double& result) {
  double raw = 0;
  if (!parseDecimal(value, raw) || !hemisphere || !*hemisphere) return false;
  const double degrees = floor(raw / 100.0);
  result = degrees + (raw - degrees * 100.0) / 60.0;
  if (*hemisphere == 'S' || *hemisphere == 'W') result = -result;
  else if (*hemisphere != 'N' && *hemisphere != 'E') return false;
  return true;
}

bool parseTime(const char* value, uint8_t& hour, uint8_t& minute, uint8_t& second) {
  if (!value || strlen(value) < 6) return false;
  char field[3] = {value[0], value[1], '\0'};
  unsigned parsedHour = 0;
  if (!parseUnsigned(field, parsedHour)) return false;
  field[0] = value[2];
  field[1] = value[3];
  unsigned parsedMinute = 0;
  if (!parseUnsigned(field, parsedMinute)) return false;
  field[0] = value[4];
  field[1] = value[5];
  unsigned parsedSecond = 0;
  if (!parseUnsigned(field, parsedSecond) || parsedHour > 23 || parsedMinute > 59 ||
      parsedSecond > 60) {
    return false;
  }
  hour = static_cast<uint8_t>(parsedHour);
  minute = static_cast<uint8_t>(parsedMinute);
  second = static_cast<uint8_t>(parsedSecond);
  return true;
}

bool parseDate(const char* value, uint16_t& year, uint8_t& month, uint8_t& day) {
  if (!value || strlen(value) != 6) return false;
  char field[3] = {value[0], value[1], '\0'};
  unsigned parsedDay = 0;
  if (!parseUnsigned(field, parsedDay)) return false;
  field[0] = value[2];
  field[1] = value[3];
  unsigned parsedMonth = 0;
  if (!parseUnsigned(field, parsedMonth)) return false;
  field[0] = value[4];
  field[1] = value[5];
  unsigned parsedYear = 0;
  if (!parseUnsigned(field, parsedYear) || parsedDay < 1 || parsedDay > 31 ||
      parsedMonth < 1 || parsedMonth > 12) {
    return false;
  }
  day = static_cast<uint8_t>(parsedDay);
  month = static_cast<uint8_t>(parsedMonth);
  year = static_cast<uint16_t>(parsedYear >= 80 ? 1900 + parsedYear : 2000 + parsedYear);
  return true;
}

int hexValue(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  return -1;
}

}  // namespace

bool State::feed(char value, uint32_t nowMs, uint32_t baudRate) {
  if (value == '$') {
    sentenceLength_ = 0;
    sentence_[sentenceLength_++] = value;
    return false;
  }
  if (!sentenceLength_) return false;
  if (value == '\r' || value == '\n') {
    sentence_[sentenceLength_] = '\0';
    const bool parsed = parseSentence(nowMs, baudRate);
    sentenceLength_ = 0;
    return parsed;
  }
  if (sentenceLength_ + 1 >= sizeof(sentence_)) {
    sentenceLength_ = 0;
    return false;
  }
  sentence_[sentenceLength_++] = value;
  return false;
}

Snapshot State::snapshot(uint32_t nowMs) const {
  Snapshot result = data_;
  result.messageAgeMs = result.lastMessageMs ? nowMs - result.lastMessageMs : 0;
  result.present = result.lastMessageMs && result.messageAgeMs <= kPresenceTimeoutMs;
  return result;
}

void State::resetParser() {
  sentenceLength_ = 0;
}

bool State::parseSentence(uint32_t nowMs, uint32_t baudRate) {
  char* checksumMarker = strchr(sentence_, '*');
  if (!checksumMarker || strlen(checksumMarker + 1) != 2) return false;
  uint8_t checksum = 0;
  for (const char* current = sentence_ + 1; current < checksumMarker; ++current) {
    checksum ^= static_cast<uint8_t>(*current);
  }
  const int high = hexValue(checksumMarker[1]);
  const int low = hexValue(checksumMarker[2]);
  if (high < 0 || low < 0 || checksum != static_cast<uint8_t>((high << 4) | low)) return false;
  *checksumMarker = '\0';

  char* fields[24]{};
  size_t fieldCount = 0;
  char* current = sentence_ + 1;
  while (current && fieldCount < sizeof(fields) / sizeof(fields[0])) {
    fields[fieldCount++] = current;
    char* comma = strchr(current, ',');
    if (!comma) break;
    *comma = '\0';
    current = comma + 1;
  }
  if (!fieldCount || strlen(fields[0]) < 3) return false;

  if (data_.lastMessageMs && nowMs - data_.lastMessageMs > kPresenceTimeoutMs) {
    data_.fixDimension = FixDimension::None;
  }
  data_.lastMessageMs = nowMs ? nowMs : 1;
  data_.baudRate = baudRate;

  const char* type = fields[0] + strlen(fields[0]) - 3;
  if (strcmp(type, "RMC") == 0 && fieldCount >= 10) {
    uint8_t hour = 0, minute = 0, second = 0, month = 0, day = 0;
    uint16_t year = 0;
    const bool haveTime = parseTime(fields[1], hour, minute, second);
    const bool haveDate = parseDate(fields[9], year, month, day);
    if (haveTime && haveDate) {
      data_.hour = hour;
      data_.minute = minute;
      data_.second = second;
      data_.year = year;
      data_.month = month;
      data_.day = day;
      data_.dateTimeValid = true;
    }
    double latitude = 0, longitude = 0;
    if (parseCoordinate(fields[3], fields[4], latitude) &&
        parseCoordinate(fields[5], fields[6], longitude)) {
      data_.latitude = latitude;
      data_.longitude = longitude;
      data_.locationValid = true;
    }
    data_.fixValid = fields[2] && fields[2][0] == 'A';
  } else if (strcmp(type, "GGA") == 0 && fieldCount >= 9) {
    uint8_t hour = 0, minute = 0, second = 0;
    if (parseTime(fields[1], hour, minute, second)) {
      data_.hour = hour;
      data_.minute = minute;
      data_.second = second;
    }
    double latitude = 0, longitude = 0;
    if (parseCoordinate(fields[2], fields[3], latitude) &&
        parseCoordinate(fields[4], fields[5], longitude)) {
      data_.latitude = latitude;
      data_.longitude = longitude;
      data_.locationValid = true;
    }
    unsigned fixQuality = 0;
    data_.fixValid = parseUnsigned(fields[6], fixQuality) && fixQuality > 0;
    unsigned satellites = 0;
    if (parseUnsigned(fields[7], satellites)) data_.satellites = static_cast<uint16_t>(satellites);
    double hdop = 0;
    data_.hdopValid = parseDecimal(fields[8], hdop);
    if (data_.hdopValid) data_.hdop = hdop;
    double altitude = 0;
    data_.altitudeValid =
        fieldCount >= 11 && strcmp(fields[10], "M") == 0 && parseDecimal(fields[9], altitude);
    if (data_.altitudeValid) data_.altitudeMeters = altitude;
  } else if (strcmp(type, "GSA") == 0 && fieldCount >= 3) {
    unsigned dimension = 0;
    if (parseUnsigned(fields[2], dimension)) {
      if (dimension >= 3) data_.fixDimension = FixDimension::ThreeD;
      else if (dimension == 2) data_.fixDimension = FixDimension::TwoD;
      else data_.fixDimension = FixDimension::None;
    }
    double hdop = 0;
    if (fieldCount >= 4 && parseDecimal(fields[fieldCount - 2], hdop)) {
      data_.hdop = hdop;
      data_.hdopValid = true;
    }
  } else if (strcmp(fields[0], "PCAS06") == 0) {
    char version[sizeof(data_.firmwareVersion)]{};
    joinFields(version, fields, 1, fieldCount);
    copyBounded(data_.firmwareVersion, *version ? version : "PCAS06 response");
  } else if (strcmp(type, "TXT") == 0 && fieldCount >= 5) {
    joinFields(data_.firmwareVersion, fields, 4, fieldCount);
  }
  return true;
}

}  // namespace gnss
