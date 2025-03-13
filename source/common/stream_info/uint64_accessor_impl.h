#pragma once

#include "envoy/stream_info/uint64_accessor.h"

namespace Envoy {
namespace StreamInfo {

/*
 * A FilterState object that tracks a single uint64_t value.
 */
class UInt64AccessorImpl : public UInt64Accessor {
public:
  UInt64AccessorImpl(uint64_t value) : value_(value) {}

  // From FilterState::Object
  ProtobufTypes::MessagePtr serializeAsProto() const override {
    auto message = std::make_unique<ProtobufWkt::UInt64Value>();
    message->set_value(value_);
    return message;
  }

  absl::optional<std::string> serializeAsString() const override { return std::to_string(value_); }

  // From UInt64Accessor.
  void increment() override { value_++; }
  uint64_t value() const override { return value_; }

private:
  uint64_t value_;
};

} // namespace StreamInfo
} // namespace Envoy
