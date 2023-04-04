#pragma once

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/geoip/geoip_provider_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Geoip {

class GeoipProviderFactoryContextImpl : public GeoipProviderFactoryContext {
public:
  GeoipProviderFactoryContextImpl(ProtobufMessage::ValidationVisitor& validation_visitor)
      : validation_visitor_(validation_visitor) {}
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return validation_visitor_;
  }

private:
  ProtobufMessage::ValidationVisitor& validation_visitor_;
};

/**
 * Common base class for geolocation provider registrations.
 */
template <class ConfigProto> class GeoipProviderFactoryBase : public GeoipProviderFactory {
public:
  // Server::Configuration::GeoipProviderFactory
  DriverSharedPtr createGeoipProviderDriver(const Protobuf::Message& config,
                                            GeoipProviderFactoryContextPtr& context) override {
    return createGeoipProviderDriverTyped(MessageUtil::downcastAndValidate<const ConfigProto&>(
                                              config, context->messageValidationVisitor()),
                                          context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override { return name_; }

protected:
  GeoipProviderFactoryBase(const std::string& name) : name_(name) {}

private:
  virtual DriverSharedPtr
  createGeoipProviderDriverTyped(const ConfigProto& proto_config,
                                 GeoipProviderFactoryContextPtr& context) PURE;

  const std::string name_;
};

} // namespace Geoip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
