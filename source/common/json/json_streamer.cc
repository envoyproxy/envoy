#include "source/common/json/json_streamer.h"

#include "source/common/json/json_sanitizer.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Json {

#ifdef NDEBUG
#define ASSERT_THIS_IS_TOP_LEVEL                                                                   \
  do {                                                                                             \
  } while (0)
#else
#define ASSERT_THIS_IS_TOP_LEVEL ASSERT(streamer_.topLevel() == this)
#endif

Streamer::Level::Level(Streamer& streamer, absl::string_view opener, absl::string_view closer)
    : streamer_(streamer), closer_(closer) {
  streamer_.addConstantString(opener);
  streamer_.push(this);
}

Streamer::Level::~Level() {
  if (!is_closed_) {
    close();
  }
}

void Streamer::Level::close() {
  if (!is_closed_) {
    ASSERT_THIS_IS_TOP_LEVEL;
    is_closed_ = true;
    streamer_.addConstantString(closer_);
    streamer_.pop(this);
  }
}

Streamer::MapPtr Streamer::makeRootMap() {
  ASSERT(levels_.empty());
  return std::make_unique<Map>(*this);
}

Streamer::MapPtr Streamer::Level::addMap() {
  ASSERT_THIS_IS_TOP_LEVEL;
  newEntry();
  return std::make_unique<Map>(streamer_);
}

Streamer::ArrayPtr Streamer::Level::addArray() {
  ASSERT_THIS_IS_TOP_LEVEL;
  newEntry();
  return std::make_unique<Array>(streamer_);
}

void Streamer::Level::addNumber(double number) {
  ASSERT_THIS_IS_TOP_LEVEL;
  newEntry();
  streamer_.addNumber(number);
}

void Streamer::Level::addString(absl::string_view str) {
  ASSERT_THIS_IS_TOP_LEVEL;
  newEntry();
  streamer_.addSanitized(str);
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
    streamer_.addConstantString(",");
  }
}

void Streamer::Map::newEntry() {
  if (expecting_value_) {
    expecting_value_ = false;
  } else {
    Level::newEntry();
  }
}

void Streamer::Map::addKey(absl::string_view key) {
  ASSERT_THIS_IS_TOP_LEVEL;
  ASSERT(!expecting_value_);
  newEntry();
  streamer_.addSanitized(key, "\":");
  expecting_value_ = true;
}

void Streamer::Map::addEntries(const Entries& entries) {
  ASSERT_THIS_IS_TOP_LEVEL;
  for (const NameValue& entry : entries) {
    newEntry();
    streamer_.addSanitized(entry.first, "\":");
    expecting_value_ = true;
    renderValue(entry.second);
  }
}

void Streamer::Level::renderValue(const Value& value) {
  switch (value.index()) {
  case 0:
    addString(absl::get<absl::string_view>(value));
    break;
  case 1:
    addNumber(absl::get<double>(value));
    break;
  default:
    IS_ENVOY_BUG(absl::StrCat("renderValue invalid index: ", value.index()));
    break;
  }
}

void Streamer::Array::addEntries(const Entries& values) {
  for (const Value& value : values) {
    renderValue(value);
  }
}

void Streamer::addNumber(double number) {
  if (std::isnan(number)) {
    response_.addFragments({"null"});
  } else {
    response_.addFragments({absl::StrCat(number)});
  }
}

void Streamer::clear() {
  while (!levels_.empty()) {
    levels_.top()->close();
  }
}

void Streamer::addSanitized(absl::string_view str, absl::string_view suffix) {
  absl::string_view sanitized = Json::sanitize(sanitize_buffer_, str);
  response_.addFragments({"\"", sanitized, suffix});
}

} // namespace Json
} // namespace Envoy
