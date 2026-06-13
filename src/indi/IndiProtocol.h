#pragma once

#include "IndiPropertyCache.h"
#include "IndiXmlTokenizer.h"

namespace indi {

class Protocol : public XmlHandler {
 public:
  explicit Protocol(PropertyCache& cache);
  void reset();
  void feed(const char* data, size_t length) { tokenizer_.feed(data, length); }
  bool overflowed() const { return tokenizer_.overflowed() || cache_.overflowed(); }

  void onStartElement(const char* name) override;
  void onAttribute(const char* name, const char* value) override;
  void onStartComplete(bool selfClosing) override;
  void onText(const char* text) override;
  void onEndElement(const char* name) override;

 private:
  static PropertyType vectorType(const char* element);
  static State parseState(const char* value);
  static Permission parsePermission(const char* value);
  static SwitchRule parseSwitchRule(const char* value);
  static bool isMemberElement(const char* element);
  static void copyDecoded(char* destination, size_t size, const char* source);
  static double parseNumber(const char* value);
  void finishMember();

  PropertyCache& cache_;
  XmlTokenizer tokenizer_;
  char element_[32]{};
  char device_[kNameSize]{};
  char propertyName_[kNameSize]{};
  char memberName_[kNameSize]{};
  char label_[kLabelSize]{};
  char group_[kGroupSize]{};
  char state_[16]{};
  char permission_[16]{};
  char rule_[16]{};
  char min_[24]{};
  char max_[24]{};
  char step_[24]{};
  char text_[kTextSize]{};
  size_t textLength_ = 0;
  Property* currentProperty_ = nullptr;
  bool blobMember_ = false;
};

}  // namespace indi
