#pragma once

#include "envoy/rds/config_traits.h"

namespace Envoy {
namespace Rds {

ProtobufTypes::MessagePtr cloneProto(ProtoTraits& proto_traits, const Protobuf::Message& rc);
std::string resourceName(ProtoTraits& proto_traits, const Protobuf::Message& rc);

} // namespace Rds
} // namespace Envoy
