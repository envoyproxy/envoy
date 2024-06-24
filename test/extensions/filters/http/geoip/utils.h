#pragma once

#include "source/extensions/filters/http/geoip/geoip_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Geoip {

class GeoipFilterPeer {
public:
  static bool useXff(const GeoipFilter& filter) { return filter.config_->useXff(); }
  static uint32_t xffNumTrustedHops(const GeoipFilter& filter) {
    return filter.config_->xffNumTrustedHops();
  }

  static const absl::flat_hash_set<std::string>& geoHeaders(const GeoipFilter& filter) {
    return filter.config_->geoHeaders();
  }
  static const absl::flat_hash_set<std::string>& geoAnonHeaders(const GeoipFilter& filter) {
    return filter.config_->geoAnonHeaders();
  }
};

} // namespace Geoip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
