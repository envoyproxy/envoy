#include "source/common/protobuf/utility.h"

#include "xds/type/v3/typed_struct.pb.h"

namespace Envoy {

absl::StatusOr<std::string> MessageUtil::knownAnyToBytes(const Protobuf::Any& any) {
  if (any.Is<Protobuf::StringValue>()) {
    Protobuf::StringValue string_value;
    RETURN_IF_NOT_OK(MessageUtil::unpackTo(any, string_value));
    return string_value.value();
  }
  if (any.Is<Protobuf::BytesValue>()) {
    Protobuf::BytesValue bytes_value;
    RETURN_IF_NOT_OK(MessageUtil::unpackTo(any, bytes_value));
    return bytesToString(bytes_value.value());
  }
  if (any.Is<Protobuf::Struct>()) {
    Protobuf::Struct struct_value;
    RETURN_IF_NOT_OK(MessageUtil::unpackTo(any, struct_value));
    return getJsonStringFromMessage(struct_value);
  }
  if (any.Is<xds::type::v3::TypedStruct>()) {
    xds::type::v3::TypedStruct typed_struct;
    RETURN_IF_NOT_OK(MessageUtil::unpackTo(any, typed_struct));
    return getJsonStringFromMessage(typed_struct.value());
  }
  return bytesToString(any.value());
}

} // namespace Envoy
