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
  if (addStringNoFlush(str)) {
    streamer_.flush();
  }
}

bool Streamer::Level::addStringNoFlush(absl::string_view str) {
  newEntry();
  return streamer_.addSanitized(str);
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
  if (streamer_.addSanitized(key, "\":")) {
    streamer_.flush();
  }
  expecting_value_ = true;
}

void Streamer::Map::addEntries(const Entries& entries) {
  ASSERT_THIS_IS_TOP_LEVEL;
  bool needs_flush = false;
  for (const NameValue& entry : entries) {
    newEntry();
    needs_flush |= streamer_.addSanitized(entry.first, "\":");
    expecting_value_ = true;
    needs_flush |= renderValueNoFlush(entry.second);
  }
  if (needs_flush) {
    streamer_.flush();
  }
}

bool Streamer::Level::renderValueNoFlush(const Value& value) {
  switch (value.index()) {
  case 0:
    return addStringNoFlush(absl::get<absl::string_view>(value));
  case 1:
    addNumber(absl::get<double>(value));
    break;
  default:
    IS_ENVOY_BUG(absl::StrCat("renderValueNoFlush invalid index: ", value.index()));
    break;
  }
  return false;
}

void Streamer::Array::addEntries(const Entries& values) {
  bool needs_flush = false;
  for (const Value& value : values) {
    needs_flush |= renderValueNoFlush(value);
  }
  if (needs_flush) {
    streamer_.flush();
  }
}

void Streamer::addNumber(double number) {
  if (std::isnan(number)) {
#if BUFFER_FRAGMENTS
    fragments_.push_back("null");
#else
    response_.addFragments({"null"});
#endif
  } else {
#if BUFFER_FRAGMENTS
    std::string& buffer = buffers_[buffers_index_];
    buffer = absl::StrCat(number); // Significantly faster than absl::StrFormat("%g", number).
    fragments_.push_back(buffer);
    nextBuffer();
#else
    response_.addFragments({absl::StrCat(number)});
#endif
  }
}

void Streamer::flush() {
#if BUFFER_FRAGMENTS
  if (fragments_.empty()) {
    return;
  }
  response_.addFragments(fragments_);
  fragments_.clear();
  buffers_index_ = 0;
#endif
}

void Streamer::clear() {
  while (!levels_.empty()) {
    levels_.top()->close();
  }
  flush();
  response_.add("");
}

bool Streamer::addSanitized(absl::string_view str, absl::string_view suffix) {
#if BUFFER_FRAGMENTS
  std::string& buffer = buffers_[buffers_index_];
#else
  std::string& buffer = buffer_;
#endif
  absl::string_view sanitized = Json::sanitize(buffer, str);
  absl::string_view fragments[] = {"\"", sanitized, suffix};
#if BUFFER_FRAGMENTS
  fragments_.insert(fragments_.end(), fragments, fragments + ABSL_ARRAYSIZE(fragments));

  // When 'str' is sanitized, the vast majority of the time, no special
  // characters are found, and the input data is returned without consuming a
  // buffer. In that case, we must let the caller know that we have added an a
  // user-controlled string to fragments_, and thus must call flush() before
  // returning control to the user.
  if (sanitized.data() != str.data()) {
    // A buffer is consumed; move to the next buffer and flush if full.
    nextBuffer();
    return false; // no flush() needed due to retaining a pointer to user memory.
  }
  return true; // indicates that flush() must be called prior to returning control.
#else
  response_.addFragments(fragments);
  return false;
#endif
}

#if BUFFER_FRAGMENTS
void Streamer::nextBuffer() {
  if (++buffers_index_ == NumBuffers) {
    flush();
  }
}
#endif

} // namespace Json
} // namespace Envoy
