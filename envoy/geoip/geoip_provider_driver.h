#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/network/address.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/factory_context.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Geolocation {

// Actual result of the lookup. Each entry in the map represents the geolocation header (entry key)
// for which the lookup was invoked mapped to a lookup result from the database.
using LookupResult = const absl::flat_hash_map<std::string, std::string>;

// Async callbacks used for geolocation provider lookups.
using LookupGeoHeadersCallback = std::function<void(LookupResult&&)>;

class LookupRequest {
public:
  LookupRequest() = default;
  LookupRequest(Network::Address::InstanceConstSharedPtr&& remote_address)
      : remote_address_(std::move(remote_address)){};

  const Network::Address::InstanceConstSharedPtr remoteAddress() const { return remote_address_; }

private:
  Network::Address::InstanceConstSharedPtr remote_address_;
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
  virtual DriverSharedPtr
  createGeoipProviderDriver(const Protobuf::Message& config, const std::string& stat_prefix,
                            Server::Configuration::FactoryContext& context) PURE;

  std::string category() const override { return "envoy.geoip_providers"; }
};

} // namespace Geolocation
} // namespace Envoy
