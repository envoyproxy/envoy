#include "source/common/json/json_streamer.h"

#include "source/common/json/json_sanitizer.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Json {

Streamer::Level::Level(Streamer& streamer, absl::string_view opener, absl::string_view closer)
    : streamer_(streamer), closer_(closer) {
  streamer_.addNoCopy(opener);
  streamer_.push(this);
}

Streamer::Level::~Level() {
  if (!is_closed_) {
    close();
  }
}

void Streamer::Level::close() {
  if (!is_closed_) {
    ASSERT(streamer_.topLevel() == this);
    is_closed_ = true;
    streamer_.addNoCopy(closer_);
    // streamer_.pop(*this);
    streamer_.pop(this);
  }
}

Streamer::MapPtr Streamer::makeRootMap() {
  ASSERT(levels_.empty());
  auto ret = std::make_unique<Map>(*this);
  // levels_.push(ret.get());
  return ret;
}

Streamer::MapPtr Streamer::Level::newMap() {
  ASSERT(streamer_.topLevel() == this);
  // auto map =
  newEntry();
  return std::make_unique<Map>(streamer_);
  // Map& ret = *map;
  // levels_.push(std::move(map));
  // return ret;
}

Streamer::ArrayPtr Streamer::Level::newArray() {
  ASSERT(streamer_.topLevel() == this);
  // auto map =
  newEntry();
  return std::make_unique<Array>(streamer_);
  // Map& ret = *map;
  // levels_.push(std::move(map));
  // return ret;
}

void Streamer::Map::Value::addSanitized(absl::string_view value) {
  ASSERT(map_.streamer_.topLevel() == &map_);
  map_.streamer_.addSanitized(value);
}

/*void Streamer::Map::entries(const Map::Entries& entries) {
  if (!levels_.empty()) {
    levels_.top()->newEntry();
  }
  std::make_unique<Map>(*this)->newEntries(entries);
  }*/

/*void Streamer::arrayEntries(const Array::Strings& strings) {
  if (!levels_.empty()) {
    levels_.top()->newEntry();
  }
  std::make_unique<Array>(*this)->newEntries(strings);
  }*/

/*Streamer::ArrayPtr Streamer::newArray() {
  //auto array =
  return std::make_unique<Array>(*this);
  //Array& ret = *array;
  //levels_.push(std::move(array));
  //return ret;
  }
*/

void Streamer::pop(Level* level) {
  ASSERT(levels_.top() == level);
  levels_.pop();
}

void Streamer::push(Level* level) { levels_.push(level); }

/*void Streamer::clear() {
  while (!levels_.empty()) {
    levels_.pop();
  }
  flush();
  }*/

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
  ASSERT(!value_);
  newEntry();
  streamer_.addFragments({"\"", name, "\":"});
  ASSERT(!expecting_value_);
  expecting_value_ = true;
  auto ret = std::make_unique<Value>(*this);
  value_ = *ret;
  return ret;
}

Streamer::Map::Value::Value(Map& map) : map_(map) { ASSERT(map_.streamer_.topLevel() == &map); }

Streamer::Map::Value::~Value() {
  if (managed_) {
    map_.clearValue();
  }
}

Streamer::Map::~Map() {
  if (value_.has_value()) {
    value_->close();
  }
}

void Streamer::Map::Value::close() {
  map_.expecting_value_ = false;
  managed_ = false;
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

void Streamer::clear() {
  while (!levels_.empty()) {
    levels_.top()->close();
  }
  flush();
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
