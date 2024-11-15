#include "source/common/rds/util.h"

namespace Envoy {
namespace Rds {

ProtobufTypes::MessagePtr cloneProto(ProtoTraits& proto_traits, const Protobuf::Message& rc) {
  auto clone = proto_traits.createEmptyProto();
  clone->CheckTypeAndMergeFrom(rc);
  return clone;
}

std::string resourceName(ProtoTraits& proto_traits, const Protobuf::Message& rc) {
  Protobuf::ReflectableMessage reflectable_message = createReflectableMessage(rc);
  const Protobuf::FieldDescriptor* field = reflectable_message->GetDescriptor()->FindFieldByNumber(
      proto_traits.resourceNameFieldNumber());
  if (!field) {
    return {};
  }
  const Protobuf::Reflection* reflection = reflectable_message->GetReflection();
  return reflection->GetString(*reflectable_message, field);
}

} // namespace Rds
} // namespace Envoy
