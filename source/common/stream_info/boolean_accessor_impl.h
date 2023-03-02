#pragma once

#include "envoy/stream_info/boolean_accessor.h"

namespace Envoy {
namespace StreamInfo {

/*
 * A FilterState object that tracks a single boolean value.
 */
class BooleanAccessorImpl : public BooleanAccessor {
public:
  BooleanAccessorImpl(bool value) : value_(value) {}

  // From FilterState::Object
  ProtobufTypes::MessagePtr serializeAsProto() const override {
    auto message = std::make_unique<ProtobufWkt::BoolValue>();
    message->set_value(value_);
    return message;
  }

  // From BooleanAccessor.
  bool value() const override { return value_; }

private:
  bool value_;
};

} // namespace StreamInfo
} // namespace Envoy
