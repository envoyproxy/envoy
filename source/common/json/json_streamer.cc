#include "source/common/json/json_streamer.h"

#include "source/common/json/json_sanitizer.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Json {

Streamer::Level::Level(Streamer& streamer, absl::string_view opener, absl::string_view closer)
    : streamer_(streamer), is_first_(true), closer_(closer) {
  streamer_.addNoCopy(opener);
}

Streamer::Level::~Level() { streamer_.addNoCopy(closer_); }

Streamer::Map& Streamer::newMap() {
  auto map = std::make_unique<Map>(*this);
  Map& ret = *map;
  levels_.push(std::move(map));
  return ret;
}

void Streamer::mapEntries(const Map::Entries& entries) {
  if (!levels_.empty()) {
    levels_.top()->newEntry();
  }
  std::make_unique<Map>(*this)->newEntries(entries);
}

void Streamer::arrayEntries(const Array::Strings& strings) {
  if (!levels_.empty()) {
    levels_.top()->newEntry();
  }
  std::make_unique<Array>(*this)->newEntries(strings);
}

Streamer::Array& Streamer::newArray() {
  auto array = std::make_unique<Array>(*this);
  Array& ret = *array;
  levels_.push(std::move(array));
  return ret;
}

void Streamer::pop(Level& level) {
  ASSERT(levels_.top().get() == &level);
  levels_.pop();
}

void Streamer::clear() {
  while (!levels_.empty()) {
    levels_.pop();
  }
  flush();
}

void Streamer::Array::newEntry() {
  if (is_first_) {
    is_first_ = false;
  } else {
    streamer_.addNoCopy(",");
  }
}

void Streamer::Map::newEntry() {
  if (expecting_value_) {
    expecting_value_ = false;
  } else if (is_first_) {
    is_first_ = false;
  } else {
    streamer_.addNoCopy(",");
  }
}

Streamer::Map::ValuePtr Streamer::Map::newKey(absl::string_view name) {
  newEntry();
  streamer_.addFragments({"\"", name, "\":"});
  ASSERT(!expecting_value_);
  expecting_value_ = true;
  return std::make_unique<Value>(*this);
}

/*void Streamer::Map::newSanitizedValue(absl::string_view value) {
  ASSERT(expecting_value_);
  streamer_.addSanitized(value);
  expecting_value_ = false;
  }*/

// void Streamer::Map::endValue() { expecting_value_ = false; }

void Streamer::Map::newEntries(const Entries& entries) {
  for (const NameValue& entry : entries) {
    newEntry();
    streamer_.addFragments({"\"", entry.first, "\":", entry.second});
  }
}

void Streamer::Array::newEntries(const Strings& entries) {
  for (absl::string_view str : entries) {
    newEntry();
    streamer_.addCopy(str);
  }
}

void Streamer::addCopy(absl::string_view str) {
  addNoCopy(str);
  flush();
}

void Streamer::addDouble(double number) {
  if (std::isnan(number)) {
    addNoCopy("null");
  } else {
    addCopy(absl::StrFormat("%g", number));
  }
}

std::string Streamer::number(double number) {
  if (std::isnan(number)) {
    return "null";
  } else {
    return absl::StrFormat("%g", number);
  }
}

std::string Streamer::quote(absl::string_view str) { return absl::StrCat("\"", str, "\""); }

void Streamer::flush() {
  response_.addFragments(fragments_);
  fragments_.clear();
}

void Streamer::addFragments(const Array::Strings& src) {
  if (fragments_.empty()) {
    response_.addFragments(src);
  } else {
    fragments_.insert(fragments_.end(), src.begin(), src.end());
    flush();
  }
}

void Streamer::addSanitized(absl::string_view token) {
  addFragments({"\"", Json::sanitize(buffer_, token), "\""});
}

} // namespace Json
} // namespace Envoy
