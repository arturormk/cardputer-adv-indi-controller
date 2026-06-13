#pragma once

#include <stddef.h>
#include <stdint.h>

namespace indi {

constexpr size_t kMaxDevices = 8;
// Conservative first-milestone limits for the generic no-PSRAM board profile.
constexpr size_t kMaxProperties = 48;
constexpr size_t kMaxMembersPerProperty = 10;
constexpr size_t kMaxLargeSwitchProperties = 1;
constexpr size_t kMaxLargeSwitchOptions = 96;
constexpr size_t kSwitchOptionNameSize = 32;
constexpr size_t kSwitchOptionLabelSize = 32;
constexpr size_t kNameSize = 48;
constexpr size_t kLabelSize = 48;
constexpr size_t kGroupSize = 40;
constexpr size_t kTextSize = 80;

enum class PropertyType : uint8_t { Number, Switch, Text, Light, Blob, Unknown };
enum class State : uint8_t { Idle, Ok, Busy, Alert, Unknown };
enum class Permission : uint8_t { ReadOnly, WriteOnly, ReadWrite, Unknown };
enum class SwitchRule : uint8_t { OneOfMany, AtMostOne, AnyOfMany, Unknown };

struct Member {
  char name[kNameSize]{};
  char label[kLabelSize]{};
  char text[kTextSize]{};
  double numberValue = 0;
  double minValue = 0;
  double maxValue = 0;
  double stepValue = 0;
  bool active = false;
};

struct SwitchOption {
  char name[kSwitchOptionNameSize]{};
  char label[kSwitchOptionLabelSize]{};
  bool active = false;
};

struct LargeSwitchProperty {
  char device[kNameSize]{};
  char name[kNameSize]{};
  SwitchOption options[kMaxLargeSwitchOptions]{};
  uint8_t optionCount = 0;
};

struct Property {
  char device[kNameSize]{};
  char name[kNameSize]{};
  char label[kLabelSize]{};
  char group[kGroupSize]{};
  PropertyType type = PropertyType::Unknown;
  State state = State::Unknown;
  Permission permission = Permission::Unknown;
  SwitchRule switchRule = SwitchRule::Unknown;
  Member members[kMaxMembersPerProperty]{};
  uint8_t memberCount = 0;
  bool defined = false;
};

struct Device {
  char name[kNameSize]{};
  uint16_t propertyCount = 0;
};

}  // namespace indi
