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

class Driver {
public:
  virtual ~Driver() = default;

  /**
   *  Looks up a city that is associated with the given IP address.
   *
   *  @param   address IP address (IPv4, IPv6) to perform lookup for.
   *  @returns city for the IP address or absl::nullopt if address is not found in the geolocation
   * database.
   */
  virtual const absl::optional<std::string>&
  getCity(const Network::Address::InstanceConstSharedPtr& address) const PURE;

  /**
   *  Looks up a country that is associated with the given IP address.
   *
   *  @param address IP address (IPv4, IPv6) to perform lookup for.
   *  @returns country ISO code for the IP address or absl::nullopt if address is not found in the
   * geolocation database.
   */
  virtual const absl::optional<std::string>&
  getCountry(const Network::Address::InstanceConstSharedPtr& address) const PURE;

  /**
   *  Looks up a region that is associated with the given IP address.
   *
   *  @param address IP address (IPv4, IPv6) to perform lookup for.
   *  @returns region ISO code for the IP address or absl::nullopt if address is not found in the
   * geolocation database.
   */
  virtual const absl::optional<std::string>&
  getRegion(const Network::Address::InstanceConstSharedPtr& address) const PURE;

  /**
   *  Looks up an ASN that is associated with the given IP address.
   *
   *  @param address IP address (IPv4, IPv6) to perform lookup for.
   *  @returns ASN of the IP provider for the IP address or absl::nullopt if address is not found in
   * the geolocation database.
   */
  virtual const absl::optional<std::string>&
  getAsn(const Network::Address::InstanceConstSharedPtr& address) const PURE;

  /**
   *  Checks if a given IP address belongs to any type of anonymization network.
   *
   *  @param address IP address (IPv4, IPv6) to perform lookup for.
   *  @returns true if IP belongs to an anonymization network, false if it does not,
   *           absl::nullopt if address is not found in the geolocation database.
   */
  virtual const absl::optional<bool>&
  getIsAnonymous(const Network::Address::InstanceConstSharedPtr& address) const PURE;

  /**
   *  Checks if a given IP address belongs to a VPN.
   *
   *  @param address IP address (IPv4, IPv6) to perform lookup for.
   *  @returns true if IP belongs to a VPN, false if it does not,
   *           absl::nullopt if address is not found in the geolocation database.
   */
  virtual const absl::optional<bool>&
  getIsAnonymousVpn(const Network::Address::InstanceConstSharedPtr& address) const PURE;

  /**
   *  Checks if a given IP address belongs to a hosting provider.
   *
   *  @param address IP address (IPv4, IPv6) to perform lookup for.
   *  @returns true if IP belongs to an anonymous hosting provider, false if it does not,
   *           absl::nullopt if address is not found in the geolocation database.
   */
  virtual const absl::optional<bool>&
  getIsAnonymousHostingProvider(const Network::Address::InstanceConstSharedPtr& address) const PURE;

  /**
   *  Checks if a given IP address belongs to a public proxy.
   *
   *  @param address IP address (IPv4, IPv6) to perform lookup for.
   *  @returns true if IP belongs to a public proxy, false if it does not,
   *           absl::nullopt if address is not found in the geolocation database.
   */
  virtual const absl::optional<bool>&
  getIsAnonymousPublicProxy(const Network::Address::InstanceConstSharedPtr& address) const PURE;

  /**
   *  Checks if a given IP address belongs to a TOR exit node.
   *
   *  @param address IP address (IPv4, IPv6) to perform lookup for.
   *  @returns true if IP belongs to a TOR exit node, false if it does not,
   *           absl::nullopt if address is not found in the geolocation database.
   */
  virtual const absl::optional<bool>&
  getIsAnonymousTorExitNode(const Network::Address::InstanceConstSharedPtr& address) const PURE;
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
