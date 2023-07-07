#include "source/common/json/json_streamer.h"

#include "source/common/json/json_sanitizer.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Json {

Streamer::Level::Level(Streamer& streamer, absl::string_view opener, absl::string_view closer)
    : streamer_(streamer), closer_(closer), is_first_(true) {
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

void Streamer::Level::newEntry() {
  if (is_first_) {
    is_first_ = false;
  } else {
    streamer_.addNoCopy(",");
  }
}

void Streamer::Map::newKey(absl::string_view name) {
  newEntry();
  streamer_.addFragments({"\"", name, "\":"});
}

void Streamer::Map::newEntries(const Entries& entries) {
  for (const NameValue& entry : entries) {
    newEntry();
    streamer_.addFragments({"\"", entry.first, "\":", entry.second});
  }
}

void Streamer::Array::newEntries(const absl::Span<const absl::string_view>& entries) {
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

std::string Streamer::quote(absl::string_view str) {
  return absl::StrCat("\"", str, "\"");
}

void Streamer::flush() {
  response_.addFragments(fragments_);
  fragments_.clear();
}

void Streamer::addFragments(absl::Span<const absl::string_view> src) {
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
