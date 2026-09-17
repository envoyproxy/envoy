#include "source/common/formatter/serializer.h"

#include "source/common/common/assert.h"
#include "source/common/json/json_utility.h"

namespace Envoy {
namespace Formatter {

ValueSink::ValueSink(JsonStringSerializer& serializer) : serializer_(serializer) {}

JsonStringSerializer* ValueSink::consume() {
  // Only a single value could be added to a given sink. Additional values would break the
  // JSON output, so they are ignored (and caught by the assertion in debug builds).
  ASSERT(serializer_.has_value(), "Multiple values are added to a single ValueSink.");
  JsonStringSerializer* serializer = serializer_.ptr();
  serializer_ = {};
  return serializer;
}

void ValueSink::addNumber(uint64_t value) {
  if (JsonStringSerializer* serializer = consume(); serializer != nullptr) {
    serializer->addNumber(value);
  }
}

void ValueSink::addNumber(double value) {
  if (JsonStringSerializer* serializer = consume(); serializer != nullptr) {
    serializer->addNumber(value);
  }
}

void ValueSink::addNumber(int64_t value) {
  if (JsonStringSerializer* serializer = consume(); serializer != nullptr) {
    serializer->addNumber(value);
  }
}

void ValueSink::addString(absl::string_view value) {
  if (JsonStringSerializer* serializer = consume(); serializer != nullptr) {
    serializer->addString(value);
  }
}

void ValueSink::addBool(bool value) {
  if (JsonStringSerializer* serializer = consume(); serializer != nullptr) {
    serializer->addBool(value);
  }
}

void ValueSink::addNull() {
  if (JsonStringSerializer* serializer = consume(); serializer != nullptr) {
    serializer->addNull();
  }
}

void ValueSink::addValue(const Protobuf::Value& value) {
  if (JsonStringSerializer* serializer = consume(); serializer != nullptr) {
    Json::Utility::appendValueToString(value, serializer->outputBuffer());
  }
}

void ValueSink::addValue(const Protobuf::Struct& value) {
  if (JsonStringSerializer* serializer = consume(); serializer != nullptr) {
    Json::Utility::appendStructToString(value, serializer->outputBuffer());
  }
}

} // namespace Formatter
} // namespace Envoy
