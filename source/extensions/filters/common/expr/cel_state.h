#pragma once

#include <string>

#include "envoy/stream_info/filter_state.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/singleton/const_singleton.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverloaded-virtual"
#endif

#include "eval/public/cel_value.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {

// CelState content declaration.
enum class CelStateType {
  Bytes,
  String,
  // Schema contains the reflection flatbuffer
  FlatBuffers,
  // Schema contains the type URL
  Protobuf,
};

// CelState type declaration.
class CelStatePrototype {
public:
  CelStatePrototype(bool readonly, CelStateType type, absl::string_view schema,
                    StreamInfo::FilterState::LifeSpan life_span)
      : readonly_(readonly), type_(type), schema_(schema), life_span_(life_span) {}
  CelStatePrototype() = default;
  const bool readonly_{false};
  const CelStateType type_{CelStateType::Bytes};
  const std::string schema_{""};
  const StreamInfo::FilterState::LifeSpan life_span_{
      StreamInfo::FilterState::LifeSpan::FilterChain};
};

using DefaultCelStatePrototype = ConstSingleton<CelStatePrototype>;
using CelStatePrototypeConstPtr = std::unique_ptr<const CelStatePrototype>;

// A simple wrapper around generic values
class CelState : public StreamInfo::FilterState::Object {
public:
  explicit CelState(const CelStatePrototype& proto)
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
  const CelStateType type_;
  absl::string_view schema_;
  std::string value_{};
  bool initialized_{false};
};

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
