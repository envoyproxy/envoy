#include "source/common/rds/util.h"

namespace Envoy {
namespace Rds {

ProtobufTypes::MessagePtr cloneProto(ProtoTraits& proto_traits, const Protobuf::Message& rc) {
  auto clone = proto_traits.createEmptyProto();
  clone->CopyFrom(rc);
  return clone;
}

} // namespace Rds
} // namespace Envoy
