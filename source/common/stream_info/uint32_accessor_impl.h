#pragma once

#include "envoy/stream_info/uint32_accessor.h"

namespace Envoy {
namespace StreamInfo {

/*
 * A FilterState object that tracks a single uint32_t value.
 */
class UInt32AccessorImpl : public UInt32Accessor {
public:
  UInt32AccessorImpl(uint32_t value) : value_(value) {}

  // From FilterState::Object
  ProtobufTypes::MessagePtr serializeAsProto() const override {
    auto message = std::make_unique<ProtobufWkt::UInt32Value>();
    message->set_value(value_);
    return message;
  }

  // From UInt32Accessor.
  void increment() override { value_++; }
  uint32_t value() const override { return value_; }

private:
  uint32_t value_;
};

} // namespace StreamInfo
} // namespace Envoy
