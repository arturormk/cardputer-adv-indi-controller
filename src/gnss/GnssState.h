#pragma once

#include <stddef.h>
#include <stdint.h>

namespace gnss {

constexpr uint32_t kPresenceTimeoutMs = 5000;

enum class FixDimension : uint8_t { None, TwoD, ThreeD };

struct Snapshot {
  bool present = false;
  bool fixValid = false;
  bool dateTimeValid = false;
  bool locationValid = false;
  bool hdopValid = false;
  bool altitudeValid = false;
  uint32_t baudRate = 0;
  uint32_t lastMessageMs = 0;
  uint32_t messageAgeMs = 0;
  uint16_t satellites = 0;
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  double latitude = 0;
  double longitude = 0;
  double hdop = 0;
  double altitudeMeters = 0;
  FixDimension fixDimension = FixDimension::None;
  char firmwareVersion[64]{};
};

class State {
 public:
  bool feed(char value, uint32_t nowMs, uint32_t baudRate);
  Snapshot snapshot(uint32_t nowMs) const;
  void resetParser();

 private:
  bool parseSentence(uint32_t nowMs, uint32_t baudRate);

  Snapshot data_{};
  char sentence_[128]{};
  size_t sentenceLength_ = 0;
};

}  // namespace gnss
