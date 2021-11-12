#pragma once

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {

// We use a tuple representation for hashing/equality/comparison, since this
// ensures we are not subject to proto nuances like unknown fields (e.g. from
// original type information annotations).
using LocalityTuple = std::tuple<const std::string&, const std::string&, const std::string&>;

struct LocalityHash {
  size_t operator()(const envoy::config::core::v3::Locality& locality) const {
    return absl::Hash<LocalityTuple>()({locality.region(), locality.zone(), locality.sub_zone()});
  }
};

struct LocalityEqualTo {
  bool operator()(const envoy::config::core::v3::Locality& lhs,
                  const envoy::config::core::v3::Locality& rhs) const {
    const LocalityTuple lhs_tuple = LocalityTuple(lhs.region(), lhs.zone(), lhs.sub_zone());
    const LocalityTuple rhs_tuple = LocalityTuple(rhs.region(), rhs.zone(), rhs.sub_zone());
    return lhs_tuple == rhs_tuple;
  }
};

struct LocalityLess {
  bool operator()(const envoy::config::core::v3::Locality& lhs,
                  const envoy::config::core::v3::Locality& rhs) const {
    const LocalityTuple lhs_tuple = LocalityTuple(lhs.region(), lhs.zone(), lhs.sub_zone());
    const LocalityTuple rhs_tuple = LocalityTuple(rhs.region(), rhs.zone(), rhs.sub_zone());
    return lhs_tuple < rhs_tuple;
  }
};

// For tests etc. where this is convenient.
static inline envoy::config::core::v3::Locality
Locality(const std::string& region, const std::string& zone, const std::string sub_zone) {
  envoy::config::core::v3::Locality locality;
  locality.set_region(region);
  locality.set_zone(zone);
  locality.set_sub_zone(sub_zone);
  return locality;
}

} // namespace Upstream
} // namespace Envoy

// Something heinous this way comes. Required to allow == for LocalityWeightsMap.h in eds.h.
namespace envoy {
namespace config {
namespace core {
namespace v3 {

inline bool operator==(const envoy::config::core::v3::Locality& x,
                       const envoy::config::core::v3::Locality& y) {
  return Envoy::Upstream::LocalityEqualTo()(x, y);
}

} // namespace v3
} // namespace core
} // namespace config
} // namespace envoy
