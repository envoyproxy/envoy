#pragma once

#include "envoy/router/string_accessor.h"

namespace Envoy {
namespace Router {

class StringAccessorImpl : public StringAccessor {
public:
  StringAccessorImpl(absl::string_view value) : value_(value) {}

  // StringAccessor
  absl::string_view asString() const override { return value_; }

  // FilterState::Object
  ProtobufTypes::MessagePtr serializeAsProto() const override {
    auto message = std::make_unique<ProtobufWkt::StringValue>();
    message->set_value(value_);
    return message;
  }

  absl::optional<std::string> serializeAsString() const override { return value_; }

private:
  std::string value_;
};

} // namespace Router
} // namespace Envoy
