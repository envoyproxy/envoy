#include "source/common/json/json_streamer.h"

#include "source/common/json/json_sanitizer.h"

namespace Envoy {
namespace Json {

Streamer::Level::Level(Streamer& streamer, absl::string_view opener, absl::string_view closer)
    : streamer_(streamer),
      closer_(closer),
      is_first_(true) {
  streamer_.addNoCopy(opener);
}

Streamer::Level::~Level() {
  streamer_.addNoCopy(closer_);
}

Streamer::Map& Streamer::newMap() {
  auto map = std::make_unique<Map>(*this);
  Map& ret = *map;
  levels_.push(std::move(map));
  return ret;
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

void Streamer::Level::newEntryHelper() {
  if (is_first_) {
    is_first_ = false;
  } else {
    streamer_.addNoCopy(",");
  }
}

void Streamer::Map::newEntry(absl::string_view name) {
  newEntryHelper();
  streamer_.addFragments({"\"", name, "\":"});
}

void Streamer::addCopy(absl::string_view str) {
  addNoCopy(str);
  flush();
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
  addCopy(Json::sanitize(buffer_, token));
}

} // namespace Json
} // namespace Envoy
