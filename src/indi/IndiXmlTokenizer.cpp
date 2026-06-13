#include "IndiXmlTokenizer.h"

#include <ctype.h>
#include <string.h>

namespace indi {

void XmlTokenizer::reset() {
  mode_ = Mode::Text;
  tokenLength_ = 0;
  quote_ = 0;
  overflowed_ = false;
}

void XmlTokenizer::append(char c) {
  if (tokenLength_ + 1 < kTokenSize) token_[tokenLength_++] = c;
  else overflowed_ = true;
}

void XmlTokenizer::finishText() {
  if (!tokenLength_) return;
  token_[tokenLength_] = '\0';
  handler_.onText(token_);
  tokenLength_ = 0;
}

void XmlTokenizer::finishStartName() {
  token_[tokenLength_] = '\0';
  handler_.onStartElement(token_);
  tokenLength_ = 0;
}

void XmlTokenizer::finishAttribute() {
  token_[tokenLength_] = '\0';
  handler_.onAttribute(attributeName_, token_);
  tokenLength_ = 0;
}

void XmlTokenizer::finishEndName() {
  token_[tokenLength_] = '\0';
  handler_.onEndElement(token_);
  tokenLength_ = 0;
}

void XmlTokenizer::feed(const char* data, size_t length) {
  for (size_t i = 0; i < length; ++i) feed(data[i]);
}

void XmlTokenizer::feed(char c) {
  switch (mode_) {
    case Mode::Text:
      if (c == '<') {
        finishText();
        mode_ = Mode::TagOpen;
      } else append(c);
      break;
    case Mode::TagOpen:
      tokenLength_ = 0;
      if (c == '/') mode_ = Mode::EndName;
      else if (c == '!' || c == '?') mode_ = Mode::Skip;
      else {
        append(c);
        mode_ = Mode::StartName;
      }
      break;
    case Mode::StartName:
      if (isspace(static_cast<unsigned char>(c))) {
        finishStartName();
        mode_ = Mode::Between;
      } else if (c == '>') {
        finishStartName();
        handler_.onStartComplete(false);
        mode_ = Mode::Text;
      } else if (c == '/') {
        finishStartName();
        mode_ = Mode::Between;
      } else append(c);
      break;
    case Mode::Between:
      if (isspace(static_cast<unsigned char>(c))) break;
      if (c == '>') {
        handler_.onStartComplete(false);
        mode_ = Mode::Text;
      } else if (c == '/') {
        handler_.onStartComplete(true);
        mode_ = Mode::Skip;
      } else {
        tokenLength_ = 0;
        append(c);
        mode_ = Mode::AttrName;
      }
      break;
    case Mode::AttrName:
      if (c == '=') {
        token_[tokenLength_] = '\0';
        strncpy(attributeName_, token_, sizeof(attributeName_) - 1);
        attributeName_[sizeof(attributeName_) - 1] = '\0';
        tokenLength_ = 0;
        mode_ = Mode::BeforeValue;
      } else if (!isspace(static_cast<unsigned char>(c))) append(c);
      break;
    case Mode::BeforeValue:
      if (c == '"' || c == '\'') {
        quote_ = c;
        mode_ = Mode::AttrValue;
      }
      break;
    case Mode::AttrValue:
      if (c == quote_) {
        finishAttribute();
        mode_ = Mode::Between;
      } else append(c);
      break;
    case Mode::EndName:
      if (c == '>') {
        finishEndName();
        mode_ = Mode::Text;
      } else if (!isspace(static_cast<unsigned char>(c))) append(c);
      break;
    case Mode::Skip:
      if (c == '>') mode_ = Mode::Text;
      break;
  }
}

}  // namespace indi
