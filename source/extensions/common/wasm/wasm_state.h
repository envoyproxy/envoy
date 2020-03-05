/*
 * Wasm State Class available to WASM/Non-WASM modules.
 */

#pragma once

#include <string>

#include "envoy/stream_info/filter_state.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

// A simple wrapper around generic values
class WasmState : public StreamInfo::FilterState::Object {
public:
  WasmState(absl::string_view type, absl::string_view value);

  explicit WasmState(absl::string_view value) : WasmState("", value) {}

  const std::string& value() const { return value_; }

  ProtobufTypes::MessagePtr serializeAsProto() const override;

private:
  const std::string type_;
  const std::string value_;
};

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
