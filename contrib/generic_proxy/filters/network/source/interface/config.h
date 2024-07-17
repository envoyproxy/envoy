#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/server/factory_context.h"

#include "contrib/generic_proxy/filters/network/source/interface/filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

using TypedExtensionConfig = envoy::config::core::v3::TypedExtensionConfig;

/**
 * Implemented by each generic filter and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class NamedFilterConfigFactory : public Config::TypedFactory {
public:
  virtual FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config, const std::string& stat_prefix,
                               Server::Configuration::FactoryContext& context) PURE;

  /**
   * @return ProtobufTypes::MessagePtr create empty route config proto message route specific
   * config.
   */
  virtual ProtobufTypes::MessagePtr createEmptyRouteConfigProto() PURE;

  /**
   * @return RouteSpecificFilterConfigConstSharedPtr allow the filter to pre-process per route
   * config. Returned object will be stored in the loaded route configuration.
   */
  virtual RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfig(const Protobuf::Message&,
                                  Server::Configuration::ServerFactoryContext&,
                                  ProtobufMessage::ValidationVisitor&) PURE;

  std::string category() const override { return "envoy.generic_proxy.filters"; }

  /**
   * @return bool true if this filter must be the last filter in a filter chain, false otherwise.
   */
  virtual bool isTerminalFilter() PURE;

  /**
   * @return absl::Status validate the codec config to see if it is compatible with the filter.
   * If the codec config is not compatible with this filter, return an error status.
   */
  virtual absl::Status validateCodec(const TypedExtensionConfig& /*config*/) {
    return absl::OkStatus();
  }

  std::set<std::string> configTypes() override {
    auto config_types = TypedFactory::configTypes();

    if (auto message = createEmptyRouteConfigProto(); message != nullptr) {
      config_types.insert(createReflectableMessage(*message)->GetDescriptor()->full_name());
    }

    return config_types;
  }
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
