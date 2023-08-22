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
    streamer_.pop(this);
  }
}

Streamer::MapPtr Streamer::makeRootMap() {
  ASSERT(levels_.empty());
  return std::make_unique<Map>(*this);
}

Streamer::MapPtr Streamer::Level::newMap() {
  ASSERT(streamer_.topLevel() == this);
  newEntry();
  return std::make_unique<Map>(streamer_);
}

Streamer::ArrayPtr Streamer::Level::newArray() {
  ASSERT(streamer_.topLevel() == this);
  newEntry();
  return std::make_unique<Array>(streamer_);
}

void Streamer::Map::addSanitized(absl::string_view value) {
  ASSERT(streamer_.topLevel() == this);
  ASSERT(expecting_value_);
  streamer_.addSanitized(value);
  expecting_value_ = false;
}

void Streamer::pop(Level* level) {
  ASSERT(levels_.top() == level);
  levels_.pop();
}

void Streamer::push(Level* level) { levels_.push(level); }

void Streamer::Level::newEntry() {
  if (is_first_) {
    is_first_ = false;
  } else {
    streamer_.addNoCopy(",");
  }
}

void Streamer::Map::newEntry() {
  if (expecting_value_) {
    expecting_value_ = false;
  } else {
    Level::newEntry();
  }
}

void Streamer::Map::newKey(absl::string_view name, std::function<void()> emit_value) {
  ASSERT(!deferred_value_);
  newEntry();
  streamer_.addFragments({"\"", name, "\":"});
  ASSERT(!expecting_value_);
  expecting_value_ = true;
  emit_value();
}

Streamer::Map::DeferredValuePtr Streamer::Map::deferValue() {
  auto ret = std::make_unique<DeferredValue>(*this);
  deferred_value_ = *ret;
  return ret;
}

Streamer::Map::DeferredValue::DeferredValue(Map& map) : map_(map) {
  ASSERT(map_.topLevel() == &map);
}

Streamer::Map::DeferredValue::~DeferredValue() {
  if (managed_) {
    map_.clearDeferredValue();
  }
}

Streamer::Map::~Map() {
  if (deferred_value_.has_value()) {
    deferred_value_->close();
  }
}

void Streamer::Map::DeferredValue::close() {
  map_.expecting_value_ = false;
  managed_ = false;
}

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
  if (fragments_.empty()) {
    return;
  }
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
