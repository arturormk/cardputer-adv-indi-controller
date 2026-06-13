#pragma once

#include <stddef.h>

namespace indi {

class XmlHandler {
 public:
  virtual ~XmlHandler() = default;
  virtual void onStartElement(const char* name) = 0;
  virtual void onAttribute(const char* name, const char* value) = 0;
  virtual void onStartComplete(bool selfClosing) = 0;
  virtual void onText(const char* text) = 0;
  virtual void onEndElement(const char* name) = 0;
};

class XmlTokenizer {
 public:
  explicit XmlTokenizer(XmlHandler& handler) : handler_(handler) {}
  void reset();
  void feed(const char* data, size_t length);
  bool overflowed() const { return overflowed_; }

 private:
  enum class Mode { Text, TagOpen, StartName, Between, AttrName, BeforeValue, AttrValue, EndName, Skip };
  void feed(char c);
  void append(char c);
  void finishText();
  void finishStartName();
  void finishAttribute();
  void finishEndName();

  static constexpr size_t kTokenSize = 128;
  XmlHandler& handler_;
  Mode mode_ = Mode::Text;
  char token_[kTokenSize]{};
  size_t tokenLength_ = 0;
  char attributeName_[kTokenSize]{};
  char quote_ = 0;
  bool overflowed_ = false;
};

}  // namespace indi

