#include "source/common/json/json_streamer.h"

#include "source/common/json/json_sanitizer.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Json {

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
    ASSERT(streamer_.topLevel() == this);
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
  ASSERT(streamer_.topLevel() == this);
  newEntry();
  return std::make_unique<Map>(streamer_);
}

Streamer::ArrayPtr Streamer::Level::addArray() {
  ASSERT(streamer_.topLevel() == this);
  newEntry();
  return std::make_unique<Array>(streamer_);
}

void Streamer::Level::addNumber(double number) {
  ASSERT(streamer_.topLevel() == this);
  newEntry();
  streamer_.addNumber(number);
}

void Streamer::Level::addString(absl::string_view str) {
  ASSERT(streamer_.topLevel() == this);
  newEntry();
  streamer_.addSanitized(str);
  streamer_.flush();
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

void Streamer::Map::newKey(absl::string_view name) {
  ASSERT(topLevel() == this);
  ASSERT(!expecting_value_);
  newEntry();
  streamer_.addSanitized(name, "\":");
  expecting_value_ = true;
}

void Streamer::Map::addEntries(const Entries& entries) {
  bool needs_flush = false;
  for (const NameValue& entry : entries) {
    newEntry();
    streamer_.addSanitized(entry.first, "\":");
    expecting_value_ = true;
    needs_flush |= renderValue(entry.second);
  }
  if (needs_flush) {
    streamer_.flush();
  }
}

bool Streamer::Level::renderValue(const Value& value) {
  switch (value.index()) {
  case 0:
    addString(absl::get<absl::string_view>(value));
    return true;
  case 1:
    addNumber(absl::get<double>(value));
    break;
  default:
    IS_ENVOY_BUG(absl::StrCat("renderValue invalid index: ", value.index()));
    break;
  }
  return false;
}

void Streamer::Array::addEntries(const Values& values) {
  bool needs_flush = false;
  for (const Value& value : values) {
    needs_flush |= renderValue(value);
  }
  if (needs_flush) {
    streamer_.flush();
  }
}

void Streamer::addNumber(double number) {
  if (std::isnan(number)) {
    fragments_.push_back("null");
  } else {
    std::string& buffer = buffers_[buffers_index_];
    buffer = absl::StrFormat("%g", number);
    fragments_.push_back(buffer);
    nextBuffer();
  }
}

void Streamer::flush() {
  if (fragments_.empty()) {
    return;
  }
  response_.addFragments(fragments_);
  fragments_.clear();
  buffers_index_ = 0;
}

void Streamer::clear() {
  while (!levels_.empty()) {
    levels_.top()->close();
  }
  flush();
  response_.add("");
}

void Streamer::addSanitized(absl::string_view token, absl::string_view suffix) {
  std::string& buffer = buffers_[buffers_index_];
  absl::string_view sanitized = Json::sanitize(buffer, token);
  absl::string_view fragments[] = {"\"", sanitized, suffix};
  fragments_.insert(fragments_.end(), fragments, fragments + ABSL_ARRAYSIZE(fragments));
  if (sanitized.data() != token.data()) {
    nextBuffer();
  }
}

void Streamer::nextBuffer() {
  if (++buffers_index_ == NumBuffers) {
    flush();
  }
}

} // namespace Json
} // namespace Envoy
