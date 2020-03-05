#include "extensions/common/wasm/wasm_state.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

WasmState::WasmState(absl::string_view type, absl::string_view value)
    : type_(type), value_(value) {}

ProtobufTypes::MessagePtr WasmState::serializeAsProto() const {
  auto any = std::make_unique<ProtobufWkt::Any>();

  if (type_.empty()) {
    ProtobufWkt::BytesValue value;
    value.set_value(value_);
    any->PackFrom(value);
  } else {
    // The WASM extension serialized in its own type.
    any->set_type_url(type_);
    any->set_value(value_);
  }

  return any;
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy