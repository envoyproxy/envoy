#pragma once

#include "envoy/common/exception.h"
#include "envoy/extensions/filters/http/geoip/v3/geoip.pb.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"

#include "source/extensions/filters/http/geoip/geoip_provider_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Geoip {

using GeolocationHeadersToAdd =
    envoy::extensions::filters::http::geoip::v3::Geoip_GeolocationHeadersToAdd;

/**
 * Configuration for the Geoip filter.
 */
class GeoipFilterConfig {
public:
  GeoipFilterConfig(const envoy::extensions::filters::http::geoip::v3::Geoip& config,
                    const std::string& stat_prefix, Stats::Scope& scope, Runtime::Loader& runtime);

  Runtime::Loader& runtime() { return runtime_; }

  void incHit(absl::string_view geo_header) {
    incCounter(stat_name_set_->getBuiltin(absl::StrCat(geo_header, ".hit"), unknown_hit_));
  }
  void incTotal(absl::string_view geo_header) {
    incCounter(stat_name_set_->getBuiltin(absl::StrCat(geo_header, ".total"), unknown_hit_));
  }

  void incTotal() { incCounter(total_); }

  bool use_xff() const { return use_xff_; }
  uint32_t xffNumTrustedHops() const { return xff_num_trusted_hops_; }

  const absl::optional<std::string>& geoCountryHeader() const { return geo_country_header_; }
  const absl::optional<std::string>& geoCityHeader() const { return geo_city_header_; }
  const absl::optional<std::string>& geoRegionHeader() const { return geo_region_header_; }
  const absl::optional<std::string>& geoAsnHeader() const { return geo_asn_header_; }
  const absl::optional<std::string>& geoAnonHeader() const { return geo_anon_header_; }
  const absl::optional<std::string>& geoAnonVpnHeader() const { return geo_anon_vpn_header_; }
  const absl::optional<std::string>& geoAnonHostingHeader() const {
    return geo_anon_hosting_header_;
  }
  const absl::optional<std::string>& geoAnonTorHeader() const { return geo_anon_tor_header_; }
  const absl::optional<std::string>& geoAnonProxyHeader() const { return geo_anon_proxy_header_; }

private:
  void incCounter(Stats::StatName name);
  void setupForGeolocationHeader(absl::optional<std::string>& geo_header,
                                 const std::string& configured_geo_header);

  Stats::Scope& scope_;
  Runtime::Loader& runtime_;
  Stats::StatNameSetPtr stat_name_set_;
  const Stats::StatName stats_prefix_;
  const Stats::StatName total_;
  const Stats::StatName unknown_hit_;
  bool use_xff_;
  const uint32_t xff_num_trusted_hops_;
  absl::optional<std::string> geo_country_header_;
  absl::optional<std::string> geo_city_header_;
  absl::optional<std::string> geo_region_header_;
  absl::optional<std::string> geo_asn_header_;
  absl::optional<std::string> geo_anon_header_;
  absl::optional<std::string> geo_anon_vpn_header_;
  absl::optional<std::string> geo_anon_hosting_header_;
  absl::optional<std::string> geo_anon_tor_header_;
  absl::optional<std::string> geo_anon_proxy_header_;
  bool at_least_one_geo_header_configured_{false};
};

using GeoipFilterConfigSharedPtr = std::shared_ptr<GeoipFilterConfig>;

class GeoipFilter : public Http::StreamDecoderFilter {
public:
  GeoipFilter(GeoipFilterConfigSharedPtr config, DriverSharedPtr driver);
  ~GeoipFilter() override;

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

private:
  void lookupGeolocationData(const absl::optional<std::string>& geo_header,
                             const Network::Address::InstanceConstSharedPtr& remote_address,
                             const absl::optional<std::string>& (Driver::*func)(
                                 const Network::Address::InstanceConstSharedPtr& address) const,
                             Http::RequestHeaderMap& headers);
  void lookupGeolocationAnonData(const absl::optional<std::string>& geo_header,
                                 const Network::Address::InstanceConstSharedPtr& remote_address,
                                 const absl::optional<bool>& (Driver::*func)(
                                     const Network::Address::InstanceConstSharedPtr& address) const,
                                 Http::RequestHeaderMap& headers);
  // Allow the unit test to have access to private members.
  friend class GeoipFilterPeer;
  GeoipFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  DriverSharedPtr driver_;
};

} // namespace Geoip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
