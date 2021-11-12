#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {

using LocalityEndpointTuple = std::tuple<const envoy::config::core::v3::Locality&,
                                         const envoy::config::endpoint::v3::LbEndpoint&>;
struct LocalityEndpointHash {
  size_t operator()(const LocalityEndpointTuple& values) const {
    const auto locality_hash = MessageUtil::hash(std::get<0>(values));
    const auto endpoint_hash = MessageUtil::hash(std::get<1>(values));
    return locality_hash ^ endpoint_hash;
  }
};

struct LocalityEndpointEqualTo {
  bool operator()(const LocalityEndpointTuple& lhs, const LocalityEndpointTuple& rhs) const {
    return Protobuf::util::MessageDifferencer::Equals(std::get<0>(lhs), std::get<0>(rhs)) &&
           Protobuf::util::MessageDifferencer::Equals(std::get<1>(lhs), std::get<1>(rhs));
  }
};

} // namespace Upstream
} // namespace Envoy
