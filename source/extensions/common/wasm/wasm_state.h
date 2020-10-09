/*
 * Wasm State Class available to Wasm/Non-Wasm modules.
 */

#pragma once

#include <string>

#include "envoy/stream_info/filter_state.h"

#include "common/protobuf/protobuf.h"
#include "common/singleton/const_singleton.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "eval/public/cel_value.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

// FilterState prefix for WasmState values.
const absl::string_view WasmStateKeyPrefix = "wasm.";

// WasmState content declaration.
enum class WasmType {
  Bytes,
  String,
  // Schema contains the reflection flatbuffer
  FlatBuffers,
  // Schema contains the type URL
  Protobuf,
};

// WasmState type declaration.
class WasmStatePrototype {
public:
  WasmStatePrototype(bool readonly, WasmType type, absl::string_view schema,
                     StreamInfo::FilterState::LifeSpan life_span)
      : readonly_(readonly), type_(type), schema_(schema), life_span_(life_span) {}
  WasmStatePrototype() = default;
  const bool readonly_{false};
  const WasmType type_{WasmType::Bytes};
  const std::string schema_{""};
  const StreamInfo::FilterState::LifeSpan life_span_{
      StreamInfo::FilterState::LifeSpan::FilterChain};
};

using DefaultWasmStatePrototype = ConstSingleton<WasmStatePrototype>;

// A simple wrapper around generic values
class WasmState : public StreamInfo::FilterState::Object {
public:
  explicit WasmState(const WasmStatePrototype& proto)
      : readonly_(proto.readonly_), type_(proto.type_), schema_(proto.schema_) {}

  const std::string& value() const { return value_; }

  // Create a value from the state, given an arena. Last argument indicates whether the value
  // is de-referenced.
  google::api::expr::runtime::CelValue exprValue(Protobuf::Arena* arena, bool last) const;

  bool setValue(absl::string_view value) {
    if (initialized_ && readonly_) {
      return false;
    }
    value_.assign(value.data(), value.size());
    initialized_ = true;
    return true;
  }

  ProtobufTypes::MessagePtr serializeAsProto() const override;
  absl::optional<std::string> serializeAsString() const override { return value_; }

private:
  const bool readonly_;
  const WasmType type_;
  absl::string_view schema_;
  std::string value_{};
  bool initialized_{false};
};

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
