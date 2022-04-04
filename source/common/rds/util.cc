#include "source/common/rds/util.h"

namespace Envoy {
namespace Rds {

ProtobufTypes::MessagePtr cloneProto(ProtoTraits& proto_traits, const Protobuf::Message& rc) {
  auto clone = proto_traits.createEmptyProto();
  clone->CopyFrom(rc);
  return clone;
}

std::string resourceName(ProtoTraits& proto_traits, const Protobuf::Message& rc) {
  const Protobuf::FieldDescriptor* field =
      rc.GetDescriptor()->FindFieldByNumber(proto_traits.resourceNameFieldNumber());
  if (!field) {
    return std::string();
  }
  const Protobuf::Reflection* reflection = rc.GetReflection();
  return reflection->GetString(rc, field);
}

} // namespace Rds
} // namespace Envoy
