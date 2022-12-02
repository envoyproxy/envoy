#include "source/extensions/filters/http/geoip/geoip_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Geoip {

class GeoipFilterPeer {
public:
  static bool useXff(const GeoipFilter& filter) { return filter.config_->use_xff(); }
  static uint32_t xffNumTrustedHops(const GeoipFilter& filter) {
    return filter.config_->xffNumTrustedHops();
  }
  static const absl::optional<std::string>& geoCountryHeader(const GeoipFilter& filter) {
    return filter.config_->geoCountryHeader();
  }
  static const absl::optional<std::string>& geoCityHeader(const GeoipFilter& filter) {
    return filter.config_->geoCityHeader();
  }
  static const absl::optional<std::string>& geoRegionHeader(const GeoipFilter& filter) {
    return filter.config_->geoRegionHeader();
  }
  static const absl::optional<std::string>& geoAsnHeader(const GeoipFilter& filter) {
    return filter.config_->geoAsnHeader();
  }
  static const absl::optional<std::string>& geoAnonVpnHeader(const GeoipFilter& filter) {
    return filter.config_->geoAnonVpnHeader();
  }
  static const absl::optional<std::string>& geoAnonHeader(const GeoipFilter& filter) {
    return filter.config_->geoAnonVpnHeader();
  }
  static const absl::optional<std::string>& geoAnonHostingHeader(const GeoipFilter& filter) {
    return filter.config_->geoAnonHostingHeader();
  }
  static const absl::optional<std::string>& geoAnonTorHeader(const GeoipFilter& filter) {
    return filter.config_->geoAnonTorHeader();
  }
  static const absl::optional<std::string>& geoAnonProxyHeader(const GeoipFilter& filter) {
    return filter.config_->geoAnonProxyHeader();
  }
};

} // namespace Geoip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
