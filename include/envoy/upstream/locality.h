#pragma once

#include "envoy/api/v2/core/base.pb.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {

// TODO(htuch): should these be templated in protobuf/utility.h?
struct LocalityHash {
  size_t operator()(const envoy::api::v2::core::Locality& locality) const {
    return MessageUtil::hash(locality);
  }
};

struct LocalityEqualTo {
  bool operator()(const envoy::api::v2::core::Locality& lhs,
                  const envoy::api::v2::core::Locality& rhs) const {
    return Protobuf::util::MessageDifferencer::Equivalent(lhs, rhs);
  }
};

struct LocalityLess {
  bool operator()(const envoy::api::v2::core::Locality& lhs,
                  const envoy::api::v2::core::Locality& rhs) const {
    using LocalityTuple = std::tuple<const std::string&, const std::string&, const std::string&>;
    const LocalityTuple lhs_tuple = LocalityTuple(lhs.region(), lhs.zone(), lhs.sub_zone());
    const LocalityTuple rhs_tuple = LocalityTuple(rhs.region(), rhs.zone(), rhs.sub_zone());
    return lhs_tuple < rhs_tuple;
  }
};

// For tests etc. where this is convenient.
static inline envoy::api::v2::core::Locality
Locality(const std::string& region, const std::string& zone, const std::string sub_zone) {
  envoy::api::v2::core::Locality locality;
  locality.set_region(region);
  locality.set_zone(zone);
  locality.set_sub_zone(sub_zone);
  return locality;
}

} // namespace Upstream
} // namespace Envoy

// Something heinous this way comes. Required to allow == for LocalityWeightsMap.h in eds.h.
namespace envoy {
namespace api {
namespace v2 {
namespace core {

inline bool operator==(const envoy::api::v2::core::Locality& x,
                       const envoy::api::v2::core::Locality& y) {
  return Envoy::Upstream::LocalityEqualTo()(x, y);
}

} // namespace core
} // namespace v2
} // namespace api
} // namespace envoy
