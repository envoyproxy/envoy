#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/extensions/filters/http/geoip/v3/geoip.pb.h"
#include "envoy/extensions/filters/http/geoip/v3/geoip.pb.validate.h"
#include "envoy/network/address.h"
#include "envoy/protobuf/message_validator.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Geoip {

// Actual result of the lookup. Each entry in the map represents the geolocation header (entry key)
// for which the lookup was invoked mapped to a lookup result from the database. Entry value will be
// set to absl::nullopt when database lookup yields an empty result.
using LookupResult = const absl::flat_hash_map<std::string, absl::optional<std::string>>;

// Async callbacks used for geolocation provider lookups.
using LookupGeoHeadersCallback = std::function<void(LookupResult&&)>;

class LookupRequest {
public:
  LookupRequest() = default;
  LookupRequest(Network::Address::InstanceConstSharedPtr&& remote_address,
                absl::flat_hash_set<std::string>&& geo_headers,
                absl::flat_hash_set<std::string>&& geo_anon_headers)
      : remote_address_(std::move(remote_address)), geo_headers_(std::move(geo_headers)),
        geo_anon_headers_(std::move(geo_anon_headers)){};

  absl::flat_hash_set<std::string> geoHeaders() const { return geo_headers_; }
  absl::flat_hash_set<std::string> geoAnonHeaders() const { return geo_anon_headers_; }
  const Network::Address::InstanceConstSharedPtr remoteAddress() const { return remote_address_; }

private:
  Network::Address::InstanceConstSharedPtr remote_address_;
  absl::flat_hash_set<std::string> geo_headers_;
  absl::flat_hash_set<std::string> geo_anon_headers_;
};

class Driver {
public:
  virtual ~Driver() = default;

  /**
   *  Performs asynchronous lookup in the geolocation database.
   *
   *  @param request containing geolocation headers to lookup in the database.
   *  @param cb supplies the filter callbacks to notify when lookup is complete.
   */
  virtual void lookup(LookupRequest&& request, LookupGeoHeadersCallback&& cb) const PURE;
};

using DriverSharedPtr = std::shared_ptr<Driver>;

/**
 * Context passed to geolocation providers to access server resources.
 */
class GeoipProviderFactoryContext {
public:
  virtual ~GeoipProviderFactoryContext() = default;

  /**
   * @return ProtobufMessage::ValidationVisitor& validation visitor for geolocation provider
   * configuration messages.
   */
  virtual ProtobufMessage::ValidationVisitor& messageValidationVisitor() PURE;
};

using GeoipProviderFactoryContextPtr = std::unique_ptr<GeoipProviderFactoryContext>;

/**
 * Implemented by each geolocation provider and registered via Registry::registerFactory() or the
 * convenience class RegisterFactory.
 */
class GeoipProviderFactory : public Config::TypedFactory {
public:
  ~GeoipProviderFactory() override = default;

  /**
   * Create a particular geolocation provider implementation. If the implementation is unable to
   * produce a geolocation provider with the provided parameters, it should throw an EnvoyException
   * in the case of general error or a Json::Exception if the json configuration is erroneous. The
   * returned pointer should always be valid.
   *
   *
   * @param config supplies the proto configuration for the geolocation provider
   * @param context supplies the factory context
   */
  virtual DriverSharedPtr createGeoipProviderDriver(const Protobuf::Message& config,
                                                    GeoipProviderFactoryContextPtr& context) PURE;

  std::string category() const override { return "envoy.geoip_providers"; }
};

} // namespace Geoip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
