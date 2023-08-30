#pragma once

#include "envoy/common/exception.h"
#include "envoy/common/optref.h"
#include "envoy/extensions/filters/http/geoip/v3/geoip.pb.h"
#include "envoy/http/filter.h"
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
                    const std::string& stat_prefix, Stats::Scope& scope);

  void incHit(absl::string_view geo_header) {
    incCounter(stat_name_set_->getBuiltin(absl::StrCat(geo_header, ".hit"), unknown_hit_));
  }
  void incTotal(absl::string_view geo_header) {
    incCounter(stat_name_set_->getBuiltin(absl::StrCat(geo_header, ".total"), unknown_hit_));
  }

  bool useXff() const { return use_xff_; }
  uint32_t xffNumTrustedHops() const { return xff_num_trusted_hops_; }
  const absl::flat_hash_set<std::string>& geoHeaders() const { return geo_headers_; }
  const absl::flat_hash_set<std::string>& geoAnonHeaders() const { return geo_anon_headers_; }

private:
  void incCounter(Stats::StatName name);
  absl::flat_hash_set<std::string>
  processGeoHeaders(const absl::flat_hash_set<absl::string_view>& headers) const;

  Stats::Scope& scope_;
  Stats::StatNameSetPtr stat_name_set_;
  const Stats::StatName stats_prefix_;
  const Stats::StatName total_;
  const Stats::StatName unknown_hit_;
  bool use_xff_;
  const uint32_t xff_num_trusted_hops_;
  absl::flat_hash_set<std::string> geo_headers_;
  absl::flat_hash_set<std::string> geo_anon_headers_;
};

using GeoipFilterConfigSharedPtr = std::shared_ptr<GeoipFilterConfig>;

class GeoipFilter : public Http::StreamDecoderFilter,
                    public Logger::Loggable<Logger::Id::filter>,
                    public std::enable_shared_from_this<GeoipFilter> {
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
  // Callbacks for geolocation filter when lookup is complete.
  void onLookupComplete(LookupResult&& result);

private:
  // Allow the unit test to have access to private members.
  friend class GeoipFilterPeer;
  GeoipFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  DriverSharedPtr driver_;
  OptRef<Http::RequestHeaderMap> request_headers_;
};

using GeoipFilterWeakPtr = std::weak_ptr<GeoipFilter>;
using GeoipFilterSharedPtr = std::shared_ptr<GeoipFilter>;

} // namespace Geoip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
