#include "source/common/json/json_streamer.h"

#include "source/common/buffer/buffer_util.h"
#include "source/common/json/json_sanitizer.h"

namespace Envoy {
namespace Json {

// To ensure the streamer is being used correctly, we use assertions to enforce
// that only the topmost map/array in the stack is being written to. To make
// this easier to do from the Level classes, we provider Streamer::topLevel() as
// a member function, but this is only needed when compiled for debug.
//
// We only compile Streamer::topLevel in debug to avoid having it be a coverage
// gap. However, assertions fail to compile in release mode if they reference
// non-existent functions or member variables, so we only compile the assertions
// in debug mode.
#ifdef NDEBUG
#define ASSERT_THIS_IS_TOP_LEVEL                                                                   \
  do {                                                                                             \
  } while (0)
#define ASSERT_LEVELS_EMPTY                                                                        \
  do {                                                                                             \
  } while (0)
#else
#define ASSERT_THIS_IS_TOP_LEVEL ASSERT(streamer_.topLevel() == this)
#define ASSERT_LEVELS_EMPTY ASSERT(levels_.empty())
#endif

Streamer::Level::Level(Streamer& streamer, absl::string_view opener, absl::string_view closer)
    : streamer_(streamer), closer_(closer) {
  streamer_.addConstantString(opener);
#ifndef NDEBUG
  streamer_.push(this);
#endif
}

Streamer::Level::~Level() {
  streamer_.addConstantString(closer_);
#ifndef NDEBUG
  streamer_.pop(this);
#endif
}

Streamer::MapPtr Streamer::makeRootMap() {
  ASSERT_LEVELS_EMPTY;
  return std::make_unique<Map>(*this);
}

Streamer::ArrayPtr Streamer::makeRootArray() {
  ASSERT_LEVELS_EMPTY;
  return std::make_unique<Array>(*this);
}

Streamer::MapPtr Streamer::Level::addMap() {
  ASSERT_THIS_IS_TOP_LEVEL;
  nextField();
  return std::make_unique<Map>(streamer_);
}

Streamer::ArrayPtr Streamer::Level::addArray() {
  ASSERT_THIS_IS_TOP_LEVEL;
  nextField();
  return std::make_unique<Array>(streamer_);
}

void Streamer::Level::addNumber(double number) {
  ASSERT_THIS_IS_TOP_LEVEL;
  nextField();
  streamer_.addNumber(number);
}

void Streamer::Level::addNumber(uint64_t number) {
  ASSERT_THIS_IS_TOP_LEVEL;
  nextField();
  streamer_.addNumber(number);
}

void Streamer::Level::addNumber(int64_t number) {
  ASSERT_THIS_IS_TOP_LEVEL;
  nextField();
  streamer_.addNumber(number);
}

void Streamer::Level::addString(absl::string_view str) {
  ASSERT_THIS_IS_TOP_LEVEL;
  nextField();
  streamer_.addSanitized("\"", str, "\"");
}

#ifndef NDEBUG
void Streamer::pop(Level* level) {
  ASSERT(levels_.top() == level);
  levels_.pop();
}

void Streamer::push(Level* level) { levels_.push(level); }
#endif

void Streamer::Level::nextField() {
  if (is_first_) {
    is_first_ = false;
  } else {
    streamer_.addConstantString(",");
  }
}

void Streamer::Map::nextField() {
  if (expecting_value_) {
    expecting_value_ = false;
  } else {
    Level::nextField();
  }
}

void Streamer::Map::addKey(absl::string_view key) {
  ASSERT_THIS_IS_TOP_LEVEL;
  ASSERT(!expecting_value_);
  nextField();
  streamer_.addSanitized("\"", key, "\":");
  expecting_value_ = true;
}

void Streamer::Map::addEntries(const Entries& entries) {
  for (const NameValue& entry : entries) {
    addKey(entry.first);
    addValue(entry.second);
  }
}

void Streamer::Level::addValue(const Value& value) {
  switch (value.index()) {
  case 0:
    addString(absl::get<absl::string_view>(value));
    break;
  case 1:
    addNumber(absl::get<double>(value));
    break;
  case 2:
    addNumber(absl::get<uint64_t>(value));
    break;
  case 3:
    addNumber(absl::get<int64_t>(value));
    break;
  default:
    IS_ENVOY_BUG(absl::StrCat("addValue invalid index: ", value.index()));
    break;
  }
}

void Streamer::Array::addEntries(const Entries& values) {
  for (const Value& value : values) {
    addValue(value);
  }
}

void Streamer::addNumber(double number) {
  if (std::isnan(number)) {
    response_.addFragments({"null"});
  } else {
    Buffer::Util::serializeDouble(number, response_);
  }
}

void Streamer::addNumber(uint64_t number) { response_.addFragments({absl::StrCat(number)}); }

void Streamer::addNumber(int64_t number) { response_.addFragments({absl::StrCat(number)}); }

void Streamer::addSanitized(absl::string_view prefix, absl::string_view str,
                            absl::string_view suffix) {
  absl::string_view sanitized = Json::sanitize(sanitize_buffer_, str);
  response_.addFragments({prefix, sanitized, suffix});
}

} // namespace Json
} // namespace Envoy
