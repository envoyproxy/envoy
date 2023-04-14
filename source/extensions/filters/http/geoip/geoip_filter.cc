#include "source/extensions/filters/http/geoip/geoip_filter.h"

#include "envoy/extensions/filters/http/geoip/v3/geoip.pb.h"

#include "source/common/http/utility.h"

#include "absl/memory/memory.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Geoip {

GeoipFilterConfig::GeoipFilterConfig(
    const envoy::extensions::filters::http::geoip::v3::Geoip& config,
    const std::string& stat_prefix, Stats::Scope& scope)
    : scope_(scope), stat_name_set_(scope.symbolTable().makeSet("Geoip")),
      stats_prefix_(stat_name_set_->add(stat_prefix + "geoip")),
      total_(stat_name_set_->add("total")), use_xff_(config.has_xff_config()),
      xff_num_trusted_hops_(config.has_xff_config() ? config.xff_config().xff_num_trusted_hops()
                                                    : 0) {
  const auto& geo_headers_to_add = config.geo_headers_to_add();
  geo_headers_ = processGeoHeaders({geo_headers_to_add.country(), geo_headers_to_add.city(),
                                    geo_headers_to_add.region(), geo_headers_to_add.asn()});
  geo_anon_headers_ =
      processGeoHeaders({geo_headers_to_add.is_anon(), geo_headers_to_add.anon_vpn(),
                         geo_headers_to_add.anon_hosting(), geo_headers_to_add.anon_tor(),
                         geo_headers_to_add.anon_proxy()});
  if (geo_headers_.empty() && geo_anon_headers_.empty()) {
    throw EnvoyException("No geolocation headers configured");
  }
}

absl::flat_hash_set<std::string>
GeoipFilterConfig::processGeoHeaders(const absl::flat_hash_set<absl::string_view>& headers) const {
  absl::flat_hash_set<std::string> geo_headers;
  for (auto header : headers) {
    if (!header.empty()) {
      stat_name_set_->rememberBuiltin(absl::StrCat(header, ".hit"));
      stat_name_set_->rememberBuiltin(absl::StrCat(header, ".total"));
      geo_headers.insert(std::string(header));
    }
  }
  return geo_headers;
}

void GeoipFilterConfig::incCounter(Stats::StatName name) {
  Stats::SymbolTable::StoragePtr storage = scope_.symbolTable().join({stats_prefix_, name});
  scope_.counterFromStatName(Stats::StatName(storage.get())).inc();
}

GeoipFilter::GeoipFilter(GeoipFilterConfigSharedPtr config, DriverSharedPtr driver)
    : config_(config), driver_(std::move(driver)) {}

GeoipFilter::~GeoipFilter() = default;

void GeoipFilter::onDestroy() {}

Http::FilterHeadersStatus GeoipFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  // Save request headers for later header manipulation once geolocation lookups are complete.
  request_headers_ = headers;

  Network::Address::InstanceConstSharedPtr remote_address;
  if (config_->useXff() && config_->xffNumTrustedHops() > 0) {
    remote_address =
        Envoy::Http::Utility::getLastAddressFromXFF(headers, config_->xffNumTrustedHops()).address_;
  }
  // If `config_->useXff() == false` or xff header has not been populated for some reason.
  if (!remote_address) {
    remote_address = decoder_callbacks_->streamInfo().downstreamAddressProvider().remoteAddress();
  }

  ASSERT(driver_, "No driver is available to perform geolocation lookup");

  // Capturing weak_ptr to GeoipFilter so that filter can be safely accessed in the posted callback.
  // This is a safe measure to protect against the case when filter gets deleted before the callback
  // is run.
  GeoipFilterWeakPtr self = weak_from_this();
  // Copy header values to pass to the driver lookup function (in case filter gets destroyed before
  // lookup completes).
  absl::flat_hash_set<std::string> geo_headers = config_->geoHeaders();
  absl::flat_hash_set<std::string> geo_anon_headers = config_->geoAnonHeaders();
  driver_->lookup(
      LookupRequest{std::move(remote_address), std::move(geo_headers), std::move(geo_anon_headers)},
      [self, &dispatcher = decoder_callbacks_->dispatcher()](LookupResult&& result) {
        dispatcher.post([self, result]() {
          if (GeoipFilterSharedPtr filter = self.lock()) {
            filter->onLookupComplete(std::move(result));
          }
        });
      });

  // Stop the iteration for headers for the current filter and the filters following.
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus GeoipFilter::decodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus GeoipFilter::decodeTrailers(Http::RequestTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void GeoipFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

void GeoipFilter::onLookupComplete(LookupResult&& result) {
  ASSERT(request_headers_);
  for (auto it = result.cbegin(); it != result.cend();) {
    const auto& geo_header = it->first;
    const auto& lookup_result = it++->second;
    if (lookup_result) {
      request_headers_->setCopy(Http::LowerCaseString(geo_header), lookup_result.value());
      config_->incHit(geo_header);
    }
    config_->incTotal(geo_header);
  }

  ENVOY_LOG(debug, "Geoip filter: finished decoding geolocation headers");
  decoder_callbacks_->continueDecoding();
}

} // namespace Geoip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
