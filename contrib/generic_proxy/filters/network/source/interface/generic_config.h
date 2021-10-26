#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/server/factory_context.h"

#include "contrib/generic_proxy/filters/network/source/interface/generic_filter.h"

namespace Envoy {
namespace Proxy {
namespace NetworkFilters {
namespace GenericProxy {

/**
 * Implemented by each generic filter and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class NamedGenericFilterConfigFactory : public Config::TypedFactory {
public:
  virtual FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config, const std::string& stat_prefix,
                               Server::Configuration::FactoryContext& context) PURE;

  /**
   * @return ProtobufTypes::MessagePtr create empty route config proto message route specfic
   * config.
   */
  virtual ProtobufTypes::MessagePtr createEmptyRouteConfigProto() { return nullptr; }

  /**
   * @return RouteSpecificFilterConfigConstSharedPtr allow the filter to pre-process per route
   * config. Returned object will be stored in the loaded route configuration.
   */
  virtual RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfig(const Protobuf::Message&, Server::Configuration::FactoryContext&,
                                  ProtobufMessage::ValidationVisitor&) {
    return nullptr;
  }

  std::string category() const override { return "proxy.filters.generic"; }

  /**
   * @return bool true if this filter must be the last filter in a filter chain, false otherwise.
   */
  virtual bool isTerminalFilter() { return false; }
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Proxy
} // namespace Envoy
