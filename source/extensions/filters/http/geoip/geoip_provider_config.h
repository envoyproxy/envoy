#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/extensions/filters/http/geoip/v3/geoip.pb.h"
#include "envoy/extensions/filters/http/geoip/v3/geoip.pb.validate.h"
#include "envoy/network/address.h"
#include "envoy/protobuf/message_validator.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Geoip {

/**
 * Async callbacks used for geolocation provider lookups.
 */
class LookupCallbacks {
public:
  virtual ~LookupCallbacks() = default;

  /**
   * Called when lookup in geolocation database is complete.
   *
   * @param lookup_result result of lookup in the geolocation database. absl::nullopt result is
   * returned when the address was not found in the database.
   * @param gelocation header name for which the lookup was invoked.
   */
  virtual void onLookupComplete(const absl::optional<std::string>& lookup_result,
                                const absl::optional<std::string>& geo_header) PURE;
};

using LookupCallbacksPtr = std::unique_ptr<LookupCallbacks>;

class Driver {
public:
  virtual ~Driver() = default;

  /**
   *  Looks up a city that is associated with the given IP address.
   *
   *  @param address IP address (IPv4, IPv6) to perform lookup for.
   *  @param callbacks supplies the callbacks to notify when lookup is complete.
   *  @param geo_header header for which the lookup is invoked. Once lookup is complete, this header
   * is passed back to filter, so that filter knows which header to decorate.
   */
  virtual void getCity(const Network::Address::InstanceConstSharedPtr& address,
                       const LookupCallbacks& callbacks,
                       const absl::optional<std::string>& geo_header) const PURE;

  /**
   *  Looks up a country ISO code that is associated with the given IP address.
   *
   *  @param address IP address (IPv4, IPv6) to perform lookup for.
   *  @param callbacks supplies the callbacks to notify when lookup is complete.
   *  @param geo_header header for which the lookup is invoked. Once lookup is complete, this header
   * is passed back to filter, so that filter knows which header to decorate.
   */
  virtual void getCountry(const Network::Address::InstanceConstSharedPtr& address,
                          const LookupCallbacks& callbacks,
                          const absl::optional<std::string>& geo_header) const PURE;

  /**
   *  Looks up a region ISO code that is associated with the given IP address.
   *
   *  @param address IP address (IPv4, IPv6) to perform lookup for.
   *  @param callbacks supplies the callbacks to notify when lookup is complete.
   *  @param geo_header header for which the lookup is invoked. Once lookup is complete, this header
   * is passed back to filter, so that filter knows which header to decorate.
   */
  virtual void getRegion(const Network::Address::InstanceConstSharedPtr& address,
                         const LookupCallbacks& callbacks,
                         const absl::optional<std::string>& geo_header) const PURE;

  /**
   *  Looks up an ASN of the IP provider that is associated with the given IP address.
   *
   *  @param address IP address (IPv4, IPv6) to perform lookup for.
   *  @param callbacks supplies the callbacks to notify when lookup is complete.
   *  @param geo_header header for which the lookup is invoked. Once lookup is complete, this header
   * is passed back to filter, so that filter knows which header to decorate.
   */
  virtual void getAsn(const Network::Address::InstanceConstSharedPtr& address,
                      const LookupCallbacks& callbacks,
                      const absl::optional<std::string>& geo_header) const PURE;

  /**
   *  Checks if a given IP address belongs to any type of anonymization network.
   *
   *  @param address IP address (IPv4, IPv6) to perform lookup for.
   *  @param callbacks supplies the callbacks to notify when lookup is complete.
   *  @param geo_header header for which the lookup is invoked. Once lookup is complete, this header
   * is passed back to filter, so that filter knows which header to decorate.
   */
  virtual void getIsAnonymous(const Network::Address::InstanceConstSharedPtr& address,
                              const LookupCallbacks& callbacks,
                              const absl::optional<std::string>& geo_header) const PURE;

  /**
   *  Checks if a given IP address belongs to a VPN.
   *
   *  @param address IP address (IPv4, IPv6) to perform lookup for.
   *  @param callbacks supplies the callbacks to notify when lookup is complete.
   *  @param geo_header header for which the lookup is invoked. Once lookup is complete, this header
   * is passed back to filter, so that filter knows which header to decorate.
   */
  virtual void getIsAnonymousVpn(const Network::Address::InstanceConstSharedPtr& address,
                                 const LookupCallbacks& callbacks,
                                 const absl::optional<std::string>& geo_header) const PURE;

  /**
   *  Checks if a given IP address belongs to an anonymous hosting provider.
   *
   *  @param address IP address (IPv4, IPv6) to perform lookup for.
   *  @param callbacks supplies the callbacks to notify when lookup is complete.
   *  @param geo_header header for which the lookup is invoked. Once lookup is complete, this header
   * is passed back to filter, so that filter knows which header to decorate.
   */
  virtual void
  getIsAnonymousHostingProvider(const Network::Address::InstanceConstSharedPtr& address,
                                const LookupCallbacks& callbacks,
                                const absl::optional<std::string>& geo_header) const PURE;

  /**
   *  Checks if a given IP address belongs to a public proxy.
   *
   *  @param address IP address (IPv4, IPv6) to perform lookup for.
   *  @param callbacks supplies the callbacks to notify when lookup is complete.
   *  @param geo_header header for which the lookup is invoked. Once lookup is complete, this header
   * is passed back to filter, so that filter knows which header to decorate.
   */
  virtual void getIsAnonymousPublicProxy(const Network::Address::InstanceConstSharedPtr& address,
                                         const LookupCallbacks& callbacks,
                                         const absl::optional<std::string>& geo_header) const PURE;

  /**
   *  Checks if a given IP address belongs to a TOR exit node.
   *
   *  @param address IP address (IPv4, IPv6) to perform lookup for.
   *  @param callbacks supplies the callbacks to notify when lookup is complete.
   *  @param geo_header header for which the lookup is invoked. Once lookup is complete, this header
   * is passed back to filter, so that filter knows which header to decorate.
   */
  virtual void getIsAnonymousTorExitNode(const Network::Address::InstanceConstSharedPtr& address,
                                         const LookupCallbacks& callbacks,
                                         const absl::optional<std::string>& geo_header) const PURE;
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
