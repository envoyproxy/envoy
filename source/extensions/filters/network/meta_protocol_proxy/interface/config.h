#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/filters/network/meta_protocol_proxy/interface/filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {

/**
 * Implemented by each meta protocol filter and registered via Registry::registerFactory or the
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

  std::string category() const override { return "envoy.meta_protocol_proxy.filters"; }

  /**
   * @return bool true if this filter must be the last filter in a filter chain, false otherwise.
   */
  virtual bool isTerminalFilter() PURE;
};

} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
