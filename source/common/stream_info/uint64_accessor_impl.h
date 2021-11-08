#pragma once

#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace StreamInfo {

/*
 * A FilterState object that tracks a single uint64_t value.
 */
class UInt64AccessorImpl : public FilterState::Object {
public:
  UInt64AccessorImpl(uint32_t value) : value_(value) {}

  // From FilterState::Object
  ProtobufTypes::MessagePtr serializeAsProto() const override {
    auto message = std::make_unique<ProtobufWkt::UInt64Value>();
    message->set_value(value_);
    return message;
  }
  uint64_t value() const { return value_; }

private:
  uint64_t value_;
};

} // namespace StreamInfo
} // namespace Envoy
