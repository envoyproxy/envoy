#pragma once

#include "envoy/geoip/geoip_provider_driver.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace GeoipProviders {
namespace Common {

/**
 * Common base class for geoip provider factory registrations. Removes a substantial amount of
 * boilerplate.
 */
template <class ConfigProto> class FactoryBase : public Geolocation::GeoipProviderFactory {
public:
  // GeoipProviderFactory
  Geolocation::DriverSharedPtr
  createGeoipProviderDriver(const Protobuf::Message& config, const std::string& stat_prefix,
                            Server::Configuration::FactoryContext& context) override {
    return createGeoipProviderDriverTyped(MessageUtil::downcastAndValidate<const ConfigProto&>(
                                              config, context.messageValidationVisitor()),
                                          stat_prefix, context);
  }

  std::string name() const override { return name_; }

  std::string category() const override { return "envoy.geoip_providers"; }

protected:
  FactoryBase(const std::string& name) : name_(name) {}

private:
  virtual Geolocation::DriverSharedPtr
  createGeoipProviderDriverTyped(const ConfigProto& proto_config, const std::string& stat_prefix,
                                 Server::Configuration::FactoryContext& context) PURE;

  const std::string name_;
};

} // namespace Common
} // namespace GeoipProviders
} // namespace Extensions
} // namespace Envoy
