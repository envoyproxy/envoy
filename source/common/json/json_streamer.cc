#include <type_traits>

#include "source/common/buffer/buffer_util.h"
#include "source/common/json/json_streamer.h"

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
#define ASSERT_THIS_IS_TOP_LEVEL ASSERT(this->streamer_.topLevel() == this)
#define ASSERT_LEVELS_EMPTY ASSERT(levels_.empty())
#endif

template <class T>
StreamerBase<T>::Level::Level(StreamerBase& streamer, absl::string_view opener,
                              absl::string_view closer)
    : streamer_(streamer), closer_(closer) {
  streamer_.addConstantString(opener);
#ifndef NDEBUG
  streamer_.push(this);
#endif
}

template <class T> StreamerBase<T>::Level::~Level() {
  streamer_.addConstantString(closer_);
#ifndef NDEBUG
  streamer_.pop(this);
#endif
}

template <class T> StreamerBase<T>::MapPtr StreamerBase<T>::makeRootMap() {
  ASSERT_LEVELS_EMPTY;
  return std::make_unique<Map>(*this);
}

template <class T> StreamerBase<T>::ArrayPtr StreamerBase<T>::makeRootArray() {
  ASSERT_LEVELS_EMPTY;
  return std::make_unique<Array>(*this);
}

template <class T> StreamerBase<T>::MapPtr StreamerBase<T>::Level::addMap() {
  ASSERT_THIS_IS_TOP_LEVEL;
  nextField();
  return std::make_unique<Map>(streamer_);
}

template <class T> StreamerBase<T>::ArrayPtr StreamerBase<T>::Level::addArray() {
  ASSERT_THIS_IS_TOP_LEVEL;
  nextField();
  return std::make_unique<Array>(streamer_);
}

template <class T> void StreamerBase<T>::Level::addNumber(double number) {
  ASSERT_THIS_IS_TOP_LEVEL;
  nextField();
  streamer_.addNumber(number);
}

template <class T> void StreamerBase<T>::Level::addNumber(uint64_t number) {
  ASSERT_THIS_IS_TOP_LEVEL;
  nextField();
  streamer_.addNumber(number);
}

template <class T> void StreamerBase<T>::Level::addNumber(int64_t number) {
  ASSERT_THIS_IS_TOP_LEVEL;
  nextField();
  streamer_.addNumber(number);
}

template <class T> void StreamerBase<T>::Level::addBool(bool b) {
  ASSERT_THIS_IS_TOP_LEVEL;
  nextField();
  streamer_.addBool(b);
}

template <class T> void StreamerBase<T>::Level::addString(absl::string_view str) {
  ASSERT_THIS_IS_TOP_LEVEL;
  nextField();
  streamer_.addSanitized("\"", str, "\"");
}

#ifndef NDEBUG
template <class T> void StreamerBase<T>::pop(Level* level) {
  ASSERT(levels_.top() == level);
  levels_.pop();
}

template <class T> void StreamerBase<T>::push(Level* level) { levels_.push(level); }
#endif

template <class T> void StreamerBase<T>::Level::nextField() {
  if (is_first_) {
    is_first_ = false;
  } else {
    streamer_.addConstantString(",");
  }
}

template <class T> void StreamerBase<T>::Map::nextField() {
  if (expecting_value_) {
    expecting_value_ = false;
  } else {
    Level::nextField();
  }
}

template <class T> void StreamerBase<T>::Map::addKey(absl::string_view key) {
  ASSERT_THIS_IS_TOP_LEVEL;
  ASSERT(!expecting_value_);
  nextField();
  this->streamer_.addSanitized("\"", key, "\":");
  expecting_value_ = true;
}

template <class T> void StreamerBase<T>::Map::addEntries(const Entries& entries) {
  for (const NameValue& entry : entries) {
    addKey(entry.first);
    this->addValue(entry.second);
  }
}

template <class T> void StreamerBase<T>::Level::addValue(const Value& value) {
  switch (value.index()) {
  case 0:
    static_assert(std::is_same<decltype(absl::get<0>(value)), const absl::string_view&>::value,
                  "value at index 0 must be an absl::string_vlew");
    addString(absl::get<absl::string_view>(value));
    break;
  case 1:
    static_assert(std::is_same<decltype(absl::get<1>(value)), const double&>::value,
                  "value at index 1 must be a double");
    addNumber(absl::get<double>(value));
    break;
  case 2:
    static_assert(std::is_same<decltype(absl::get<2>(value)), const uint64_t&>::value,
                  "value at index 2 must be a uint64_t");
    addNumber(absl::get<uint64_t>(value));
    break;
  case 3:
    static_assert(std::is_same<decltype(absl::get<3>(value)), const int64_t&>::value,
                  "value at index 3 must be an int64_t");
    addNumber(absl::get<int64_t>(value));
    break;
  case 4:
    static_assert(std::is_same<decltype(absl::get<4>(value)), const bool&>::value,
                  "value at index 4 must be a bool");
    addBool(absl::get<bool>(value));
    break;
  default:
    IS_ENVOY_BUG(absl::StrCat("addValue invalid index: ", value.index()));
    break;
  }
}

template <class T> void StreamerBase<T>::Array::addEntries(const Entries& values) {
  for (const Value& value : values) {
    this->addValue(value);
  }
}

template <class T> void StreamerBase<T>::addNumber(double number) {
  if (std::isnan(number)) {
    output_.addFragments({"null"});
  } else {
    Buffer::Util::serializeDouble(number, output_);
  }
}

template <class T> void StreamerBase<T>::addNumber(uint64_t number) {
  output_.addFragments({absl::StrCat(number)});
}

template <class T> void StreamerBase<T>::addNumber(int64_t number) {
  output_.addFragments({absl::StrCat(number)});
}

template <class T> void StreamerBase<T>::addBool(bool b) {
  output_.addFragments({b ? "true" : "false"});
}

template <class T>
void StreamerBase<T>::addSanitized(absl::string_view prefix, absl::string_view str,
                                   absl::string_view suffix) {
  absl::string_view sanitized = Json::sanitize(sanitize_buffer_, str);
  output_.addFragments({prefix, sanitized, suffix});
}

} // namespace Json
} // namespace Envoy
