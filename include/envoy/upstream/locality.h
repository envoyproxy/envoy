#pragma once

#include "envoy/config/core/v3alpha/base.pb.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {

// TODO(htuch): should these be templated in protobuf/utility.h?
struct LocalityHash {
  size_t operator()(const envoy::config::core::v3alpha::Locality& locality) const {
    return MessageUtil::hash(locality);
  }
};

struct LocalityEqualTo {
  bool operator()(const envoy::config::core::v3alpha::Locality& lhs,
                  const envoy::config::core::v3alpha::Locality& rhs) const {
    return Protobuf::util::MessageDifferencer::Equivalent(lhs, rhs);
  }
};

struct LocalityLess {
  bool operator()(const envoy::config::core::v3alpha::Locality& lhs,
                  const envoy::config::core::v3alpha::Locality& rhs) const {
    using LocalityTuple = std::tuple<const std::string&, const std::string&, const std::string&>;
    const LocalityTuple lhs_tuple = LocalityTuple(lhs.region(), lhs.zone(), lhs.sub_zone());
    const LocalityTuple rhs_tuple = LocalityTuple(rhs.region(), rhs.zone(), rhs.sub_zone());
    return lhs_tuple < rhs_tuple;
  }
};

// For tests etc. where this is convenient.
static inline envoy::config::core::v3alpha::Locality
Locality(const std::string& region, const std::string& zone, const std::string sub_zone) {
  envoy::config::core::v3alpha::Locality locality;
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
namespace v3alpha {

inline bool operator==(const envoy::config::core::v3alpha::Locality& x,
                       const envoy::config::core::v3alpha::Locality& y) {
  return Envoy::Upstream::LocalityEqualTo()(x, y);
}

} // namespace v3alpha
} // namespace core
} // namespace config
} // namespace envoy
