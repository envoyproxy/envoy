#pragma once

#include "envoy/rds/config_traits.h"

#include "source/common/protobuf/arena_wrapped_proto.h"

namespace Envoy {
namespace Rds {

ArenaWrappedProto<Protobuf::Message> cloneProto(ProtoTraits& proto_traits,
                                                const Protobuf::Message& rc);
std::string resourceName(ProtoTraits& proto_traits, const Protobuf::Message& rc);

} // namespace Rds
} // namespace Envoy
