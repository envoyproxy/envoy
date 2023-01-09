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

absl::optional<std::string> GeoipFilterConfig::setupForGeolocationHeader(absl::string_view configured_geo_header) {
  if (!configured_geo_header.empty()) {
    stat_name_set_->rememberBuiltin(absl::StrCat(configured_geo_header, ".hit"));
    stat_name_set_->rememberBuiltin(absl::StrCat(configured_geo_header, ".total"));
    at_least_one_geo_header_configured_ = true;
    return configured_geo_header;
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
  Network::Address::InstanceConstSharedPtr remote_address;
  if (config_->use_xff() && config_->xffNumTrustedHops() > 0) {
    remote_address =
        Envoy::Http::Utility::getLastAddressFromXFF(headers, config_->xffNumTrustedHops()).address_;
  }
  // If `config_->use_xff() == false` or xff header has not been populated for some reason.
  if (!remote_address) {
    remote_address = callbacks_->streamInfo().downstreamAddressProvider().remoteAddress();
  }
  lookupGeolocationData(config_->geoCountryHeader(), remote_address, &Driver::getCountry, headers);
  lookupGeolocationData(config_->geoCityHeader(), remote_address, &Driver::getCity, headers);
  lookupGeolocationData(config_->geoRegionHeader(), remote_address, &Driver::getRegion, headers);
  lookupGeolocationData(config_->geoAsnHeader(), remote_address, &Driver::getAsn, headers);
  // todo(nezdolik) switch to batch lookup for anon data. Such lookup is supported by maxmind lib.
  lookupGeolocationAnonData(config_->geoAnonHeader(), remote_address, &Driver::getIsAnonymous,
                            headers);
  lookupGeolocationAnonData(config_->geoAnonVpnHeader(), remote_address, &Driver::getIsAnonymousVpn,
                            headers);
  lookupGeolocationAnonData(config_->geoAnonHostingHeader(), remote_address,
                            &Driver::getIsAnonymousHostingProvider, headers);
  lookupGeolocationAnonData(config_->geoAnonTorHeader(), remote_address,
                            &Driver::getIsAnonymousTorExitNode, headers);
  lookupGeolocationAnonData(config_->geoAnonProxyHeader(), remote_address,
                            &Driver::getIsAnonymousPublicProxy, headers);

  return Http::FilterHeadersStatus::Continue;
}

void GeoipFilter::lookupGeolocationData(
    const absl::optional<std::string>& geo_header,
    const Network::Address::InstanceConstSharedPtr& remote_address,
    const absl::optional<std::string>& (Driver::*func)(
        const Network::Address::InstanceConstSharedPtr& address) const,
    Http::RequestHeaderMap& headers) {
  if (geo_header) {
    auto lookupResult = ((*driver_).*func)(remote_address);
    if (lookupResult) {
      headers.setCopy(Http::LowerCaseString(geo_header.value()), lookupResult.value());
      config_->incHit(geo_header.value());
    }
    config_->incTotal(geo_header.value());
  }
}

void GeoipFilter::lookupGeolocationAnonData(
    const absl::optional<std::string>& geo_header,
    const Network::Address::InstanceConstSharedPtr& remote_address,
    const absl::optional<bool>& (Driver::*func)(
        const Network::Address::InstanceConstSharedPtr& address) const,
    Http::RequestHeaderMap& headers) {
  if (geo_header) {
    auto lookupResult = ((*driver_).*func)(remote_address);
    if (lookupResult) {
      headers.addCopy(Http::LowerCaseString(geo_header.value()), lookupResult.value() ? "true" : "false");
      config_->incHit(geo_header.value());
    }
    config_->incTotal(geo_header.value());
  }
}

Http::FilterDataStatus GeoipFilter::decodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus GeoipFilter::decodeTrailers(Http::RequestTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void GeoipFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

} // namespace Geoip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
