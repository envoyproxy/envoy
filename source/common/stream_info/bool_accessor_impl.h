#pragma once

#include "envoy/stream_info/bool_accessor.h"

namespace Envoy {
namespace StreamInfo {

/*
 * A FilterState object that tracks a single boolean value.
 */
class BoolAccessorImpl : public BoolAccessor {
public:
  BoolAccessorImpl(bool value) : value_(value) {}

  // From FilterState::Object
  ProtobufTypes::MessagePtr serializeAsProto() const override {
    auto message = std::make_unique<ProtobufWkt::BoolValue>();
    message->set_value(value_);
    return message;
  }

  absl::optional<std::string> serializeAsString() const override {
    return value_ ? "true" : "false";
  }

  // From BoolAccessor.
  bool value() const override { return value_; }

private:
  bool value_;
};

} // namespace StreamInfo
} // namespace Envoy
