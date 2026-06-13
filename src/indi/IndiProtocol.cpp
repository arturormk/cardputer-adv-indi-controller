#include "IndiProtocol.h"

#include <stdlib.h>
#include <string.h>

namespace indi {
namespace {

template <size_t N>
void copyRaw(char (&destination)[N], const char* source) {
  strncpy(destination, source ? source : "", N - 1);
  destination[N - 1] = '\0';
}

bool startsWith(const char* value, const char* prefix) {
  return strncmp(value, prefix, strlen(prefix)) == 0;
}

}  // namespace

Protocol::Protocol(PropertyCache& cache) : cache_(cache), tokenizer_(*this) {}

void Protocol::reset() {
  tokenizer_.reset();
  currentProperty_ = nullptr;
  element_[0] = device_[0] = propertyName_[0] = memberName_[0] = '\0';
  textLength_ = 0;
  blobMember_ = false;
}

PropertyType Protocol::vectorType(const char* element) {
  const char* type = startsWith(element, "def") || startsWith(element, "set")
                         ? element + 3
                         : element;
  if (startsWith(type, "NumberVector")) return PropertyType::Number;
  if (startsWith(type, "SwitchVector")) return PropertyType::Switch;
  if (startsWith(type, "TextVector")) return PropertyType::Text;
  if (startsWith(type, "LightVector")) return PropertyType::Light;
  if (startsWith(type, "BLOBVector")) return PropertyType::Blob;
  return PropertyType::Unknown;
}

State Protocol::parseState(const char* value) {
  if (!strcmp(value, "Idle")) return State::Idle;
  if (!strcmp(value, "Ok")) return State::Ok;
  if (!strcmp(value, "Busy")) return State::Busy;
  if (!strcmp(value, "Alert")) return State::Alert;
  return State::Unknown;
}

Permission Protocol::parsePermission(const char* value) {
  if (!strcmp(value, "ro")) return Permission::ReadOnly;
  if (!strcmp(value, "wo")) return Permission::WriteOnly;
  if (!strcmp(value, "rw")) return Permission::ReadWrite;
  return Permission::Unknown;
}

SwitchRule Protocol::parseSwitchRule(const char* value) {
  if (!strcmp(value, "OneOfMany")) return SwitchRule::OneOfMany;
  if (!strcmp(value, "AtMostOne")) return SwitchRule::AtMostOne;
  if (!strcmp(value, "AnyOfMany")) return SwitchRule::AnyOfMany;
  return SwitchRule::Unknown;
}

bool Protocol::isMemberElement(const char* element) {
  return !strcmp(element, "defNumber") || !strcmp(element, "oneNumber") ||
         !strcmp(element, "defSwitch") || !strcmp(element, "oneSwitch") ||
         !strcmp(element, "defText") || !strcmp(element, "oneText") ||
         !strcmp(element, "defLight") || !strcmp(element, "oneLight") ||
         !strcmp(element, "defBLOB") || !strcmp(element, "oneBLOB");
}

void Protocol::copyDecoded(char* destination, size_t size, const char* source) {
  if (!size) return;
  size_t output = 0;
  for (size_t i = 0; source && source[i] && output + 1 < size;) {
    if (source[i] == '&') {
      struct Entity { const char* encoded; char decoded; };
      static const Entity entities[] = {
          {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
      bool matched = false;
      for (const auto& entity : entities) {
        const size_t length = strlen(entity.encoded);
        if (strncmp(source + i, entity.encoded, length) == 0) {
          destination[output++] = entity.decoded;
          i += length;
          matched = true;
          break;
        }
      }
      if (matched) continue;
    }
    destination[output++] = source[i++];
  }
  destination[output] = '\0';
}

double Protocol::parseNumber(const char* value) {
  return value && *value ? strtod(value, nullptr) : 0;
}

void trim(char* value) {
  char* start = value;
  while (*start && static_cast<unsigned char>(*start) <= ' ') ++start;
  if (start != value) memmove(value, start, strlen(start) + 1);
  size_t length = strlen(value);
  while (length && static_cast<unsigned char>(value[length - 1]) <= ' ') value[--length] = '\0';
}

void Protocol::onStartElement(const char* name) {
  copyRaw(element_, name);
  device_[0] = propertyName_[0] = memberName_[0] = label_[0] = group_[0] = '\0';
  state_[0] = permission_[0] = rule_[0] = min_[0] = max_[0] = step_[0] = '\0';
  textLength_ = 0;
  text_[0] = '\0';
  blobMember_ = !strcmp(name, "oneBLOB") || !strcmp(name, "defBLOB");
}

void Protocol::onAttribute(const char* name, const char* value) {
  if (!strcmp(name, "device")) copyDecoded(device_, sizeof(device_), value);
  else if (!strcmp(name, "name")) copyDecoded(propertyName_, sizeof(propertyName_), value);
  else if (!strcmp(name, "label")) copyDecoded(label_, sizeof(label_), value);
  else if (!strcmp(name, "group")) copyDecoded(group_, sizeof(group_), value);
  else if (!strcmp(name, "state")) copyRaw(state_, value);
  else if (!strcmp(name, "perm")) copyRaw(permission_, value);
  else if (!strcmp(name, "rule")) copyRaw(rule_, value);
  else if (!strcmp(name, "min")) copyRaw(min_, value);
  else if (!strcmp(name, "max")) copyRaw(max_, value);
  else if (!strcmp(name, "step")) copyRaw(step_, value);
}

void Protocol::onStartComplete(bool selfClosing) {
  const PropertyType type = vectorType(element_);
  if (type != PropertyType::Unknown) {
    currentProperty_ = cache_.upsertProperty(device_, propertyName_, type);
    if (currentProperty_) {
      if (*label_) copyDecoded(currentProperty_->label, sizeof(currentProperty_->label), label_);
      if (*group_) copyDecoded(currentProperty_->group, sizeof(currentProperty_->group), group_);
      if (*state_) currentProperty_->state = parseState(state_);
      if (*permission_) currentProperty_->permission = parsePermission(permission_);
      if (*rule_) currentProperty_->switchRule = parseSwitchRule(rule_);
    }
  } else if (isMemberElement(element_)) {
    copyRaw(memberName_, propertyName_);
  } else if (!strcmp(element_, "delProperty")) {
    cache_.remove(device_, propertyName_);
  }
  if (selfClosing && isMemberElement(element_)) finishMember();
}

void Protocol::onText(const char* text) {
  if (blobMember_ || !isMemberElement(element_)) return;
  while (*text && textLength_ + 1 < sizeof(text_)) text_[textLength_++] = *text++;
  text_[textLength_] = '\0';
}

void Protocol::finishMember() {
  if (!currentProperty_ || !*memberName_) return;
  trim(text_);
  if (currentProperty_->type == PropertyType::Switch) {
    cache_.upsertLargeSwitchOption(*currentProperty_, memberName_, label_, !strcmp(text_, "On"));
  }
  Member* member = cache_.upsertMember(*currentProperty_, memberName_);
  if (!member) return;
  if (*label_) copyDecoded(member->label, sizeof(member->label), label_);
  copyDecoded(member->text, sizeof(member->text), text_);
  if (currentProperty_->type == PropertyType::Number) {
    member->numberValue = parseNumber(text_);
    member->minValue = parseNumber(min_);
    member->maxValue = parseNumber(max_);
    member->stepValue = parseNumber(step_);
  } else if (currentProperty_->type == PropertyType::Switch) {
    member->active = !strcmp(text_, "On");
  }
}

void Protocol::onEndElement(const char* name) {
  if (isMemberElement(name)) finishMember();
  if (vectorType(name) != PropertyType::Unknown) currentProperty_ = nullptr;
  element_[0] = '\0';
  blobMember_ = false;
  textLength_ = 0;
}

}  // namespace indi
