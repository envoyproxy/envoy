#include "source/extensions/filters/common/expr/cel_state.h"

#include "eval/public/structs/cel_proto_wrapper.h"
#include "flatbuffers/reflection.h"
#include "tools/flatbuffers_backed_impl.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {

using google::api::expr::runtime::CelValue;

CelValue CelState::exprValue(Protobuf::Arena* arena, bool last) const {
  if (initialized_) {
    switch (type_) {
    case CelStateType::String:
      return CelValue::CreateString(&value_);
    case CelStateType::Bytes:
      return CelValue::CreateBytes(&value_);
    case CelStateType::Protobuf: {
      if (last) {
        return CelValue::CreateBytes(&value_);
      }
      // Note that this is very expensive since it incurs a de-serialization
      const auto any = serializeAsProto();
      return google::api::expr::runtime::CelProtoWrapper::CreateMessage(any.get(), arena);
    }
    case CelStateType::FlatBuffers:
      if (last) {
        return CelValue::CreateBytes(&value_);
      }
      return CelValue::CreateMap(google::api::expr::runtime::CreateFlatBuffersBackedObject(
          reinterpret_cast<const uint8_t*>(value_.data()), *reflection::GetSchema(schema_.data()),
          arena));
    }
  }
  return CelValue::CreateNull();
}

ProtobufTypes::MessagePtr CelState::serializeAsProto() const {
  auto any = std::make_unique<ProtobufWkt::Any>();

  if (type_ != CelStateType::Protobuf) {
    ProtobufWkt::BytesValue value;
    value.set_value(value_);
    any->PackFrom(value);
  } else {
    any->set_type_url(std::string(schema_));
    any->set_value(value_);
  }

  return any;
}

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
