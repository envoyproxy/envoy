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
      stats_prefix_(stat_name_set_->add(stat_prefix + "geoip")), use_xff_(config.has_xff_config()),
      xff_num_trusted_hops_(config.has_xff_config() ? config.xff_config().xff_num_trusted_hops()
                                                    : 0) {
  stat_name_set_->rememberBuiltin("total");
}

void GeoipFilterConfig::incCounter(Stats::StatName name) {
  Stats::SymbolTable::StoragePtr storage = scope_.symbolTable().join({stats_prefix_, name});
  scope_.counterFromStatName(Stats::StatName(storage.get())).inc();
}

GeoipFilter::GeoipFilter(GeoipFilterConfigSharedPtr config, Geolocation::DriverSharedPtr driver)
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
  driver_->lookup(
      Geolocation::LookupRequest{std::move(remote_address)},
      [self, &dispatcher = decoder_callbacks_->dispatcher()](Geolocation::LookupResult&& result) {
        dispatcher.post([self, result]() {
          if (GeoipFilterSharedPtr filter = self.lock()) {
            filter->onLookupComplete(std::move(result));
          }
        });
      });

  // Stop the iteration for headers and data (POST request) for the current filter and the filters
  // following.
  return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
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

void GeoipFilter::onLookupComplete(Geolocation::LookupResult&& result) {
  ASSERT(request_headers_);
  for (auto it = result.cbegin(); it != result.cend();) {
    const auto& geo_header = it->first;
    const auto& lookup_result = it++->second;
    if (!lookup_result.empty()) {
      request_headers_->setCopy(Http::LowerCaseString(geo_header), lookup_result);
    }
  }
  config_->incTotal();
  ENVOY_LOG(debug, "Geoip filter: finished decoding geolocation headers");
  decoder_callbacks_->continueDecoding();
}

} // namespace Geoip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
