#include "source/extensions/filters/http/geoip/geoip_filter.h"

#include "envoy/extensions/filters/http/geoip/v3/geoip.pb.h"

#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Geoip {

GeoipFilterConfig::GeoipFilterConfig(
    const envoy::extensions::filters::http::geoip::v3::Geoip& config,
    const std::string& stat_prefix, Stats::Scope& scope, Runtime::Loader& runtime)
    : scope_(scope), runtime_(runtime), stat_name_set_(scope.symbolTable().makeSet("Geoip")),
      stats_prefix_(stat_name_set_->add(stat_prefix + "geoip")),
      total_(stat_name_set_->add("total")), use_xff_(config.use_xff()),
      xff_num_trusted_hops_(config.xff_num_trusted_hops()) {
  auto geo_headers_to_add = config.geo_headers_to_add();
  geo_country_header_ = setupForGeolocationHeader(geo_headers_to_add.country());
  geo_city_header_ = setupForGeolocationHeader(geo_headers_to_add.city());
  geo_region_header_ = setupForGeolocationHeader(geo_headers_to_add.region());
  geo_asn_header_ = setupForGeolocationHeader(geo_headers_to_add.asn());
  geo_anon_header_ = setupForGeolocationHeader(geo_headers_to_add.is_anon());
  geo_anon_vpn_header_ = setupForGeolocationHeader(geo_headers_to_add.anon_vpn());
  geo_anon_hosting_header_ = setupForGeolocationHeader(geo_headers_to_add.anon_hosting());
  geo_anon_tor_header_ = setupForGeolocationHeader(geo_headers_to_add.anon_tor());
  geo_anon_proxy_header_ = setupForGeolocationHeader(geo_headers_to_add.anon_proxy());
  if (!at_least_one_geo_header_configured_) {
    throw EnvoyException("No geolocation headers configured");
  }
}

absl::optional<std::string>
GeoipFilterConfig::setupForGeolocationHeader(absl::string_view configured_geo_header) {
  if (!configured_geo_header.empty()) {
    stat_name_set_->rememberBuiltin(absl::StrCat(configured_geo_header, ".hit"));
    stat_name_set_->rememberBuiltin(absl::StrCat(configured_geo_header, ".total"));
    at_least_one_geo_header_configured_ = true;
    return absl::make_optional(std::string(configured_geo_header));
  } else {
    return absl::nullopt;
  }
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
  // Reset these flags for geolocation callbacks.
  all_lookups_invoked_ = false;
  num_lookups_to_perform_ = 0;
  // Save request headers for later header manipulation once geolocation lookups are complete.
  request_headers_ = &headers;

  Network::Address::InstanceConstSharedPtr remote_address;
  if (config_->use_xff() && config_->xffNumTrustedHops() > 0) {
    remote_address =
        Envoy::Http::Utility::getLastAddressFromXFF(headers, config_->xffNumTrustedHops()).address_;
  }
  // If `config_->use_xff() == false` or xff header has not been populated for some reason.
  if (!remote_address) {
    remote_address = decoder_callbacks_->streamInfo().downstreamAddressProvider().remoteAddress();
  }
  lookupGeolocationData(config_->geoCountryHeader(), remote_address, &Driver::getCountry);
  lookupGeolocationData(config_->geoCityHeader(), remote_address, &Driver::getCity);
  lookupGeolocationData(config_->geoRegionHeader(), remote_address, &Driver::getRegion);
  lookupGeolocationData(config_->geoAsnHeader(), remote_address, &Driver::getAsn);
  // todo(nezdolik) switch to batch lookup for anon data. Such lookup is supported by maxmind lib.
  lookupGeolocationAnonData(config_->geoAnonHeader(), remote_address, &Driver::getIsAnonymous);
  lookupGeolocationAnonData(config_->geoAnonVpnHeader(), remote_address,
                            &Driver::getIsAnonymousVpn);
  lookupGeolocationAnonData(config_->geoAnonHostingHeader(), remote_address,
                            &Driver::getIsAnonymousHostingProvider);
  lookupGeolocationAnonData(config_->geoAnonTorHeader(), remote_address,
                            &Driver::getIsAnonymousTorExitNode);
  lookupGeolocationAnonData(config_->geoAnonProxyHeader(), remote_address,
                            &Driver::getIsAnonymousPublicProxy);
  all_lookups_invoked_ = true;

  // Stop the iteration for headers for the current filter and the filters following.
  return allLookupsComplete() ? Http::FilterHeadersStatus::Continue
                              : Http::FilterHeadersStatus::StopIteration;
}

void GeoipFilter::lookupGeolocationData(
    const absl::optional<std::string>& geo_header,
    const Network::Address::InstanceConstSharedPtr& remote_address,
    void (Driver::*func)(const Network::Address::InstanceConstSharedPtr& address,
                         const LookupCallbacks& callbacks,
                         const absl::optional<std::string>& geo_header) const) {
  if (geo_header) {
    ++num_lookups_to_perform_;
    ((*driver_).*func)(remote_address, *this, geo_header);
  }
}

void GeoipFilter::lookupGeolocationAnonData(
    const absl::optional<std::string>& geo_header,
    const Network::Address::InstanceConstSharedPtr& remote_address,
    void (Driver::*func)(const Network::Address::InstanceConstSharedPtr& address,
                         const LookupCallbacks& callbacks,
                         const absl::optional<std::string>& geo_header) const) {
  if (geo_header) {
    ++num_lookups_to_perform_;
    ((*driver_).*func)(remote_address, *this, geo_header);
  }
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

void GeoipFilter::onLookupComplete(const absl::optional<std::string>& lookup_result,
                                   const absl::optional<std::string>& geo_header) {
  if (!request_headers_) {
    ENVOY_LOG(debug, "Geoip filter: no header to decorate in reguest");
    return;
  }
  ASSERT(num_lookups_to_perform_ > 0);
  --num_lookups_to_perform_;
  if (lookup_result) {
    request_headers_->setCopy(Http::LowerCaseString(geo_header.value()), lookup_result.value());
    config_->incHit(geo_header.value());
  }
  config_->incTotal(geo_header.value());
  if (allLookupsComplete()) {
    ENVOY_LOG(debug, "Geoip filter: finished decoding geolocation headers");
    decoder_callbacks_->continueDecoding();
  }
}

bool GeoipFilter::allLookupsComplete() {
  return all_lookups_invoked_ && num_lookups_to_perform_ == 0;
}

} // namespace Geoip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
