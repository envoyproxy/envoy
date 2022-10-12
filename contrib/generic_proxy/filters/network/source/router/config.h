#pragma once

#include "contrib/envoy/extensions/filters/network/generic_proxy/router/v3/router.pb.h"
#include "contrib/envoy/extensions/filters/network/generic_proxy/router/v3/router.pb.validate.h"
#include "contrib/generic_proxy/filters/network/source/interface/config.h"
#include "contrib/generic_proxy/filters/network/source/router/router.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Router {

class RouterFactory : public NamedFilterConfigFactory {
public:
  FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config, const std::string& stat_prefix,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::filters::network::generic_proxy::router::v3::Router>();
  }
  ProtobufTypes::MessagePtr createEmptyRouteConfigProto() override { return nullptr; }
  RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfig(const Protobuf::Message&,
                                  Server::Configuration::ServerFactoryContext&,
                                  ProtobufMessage::ValidationVisitor&) override {
    return nullptr;
  }
  bool isTerminalFilter() override { return true; }

  std::string name() const override { return "envoy.filters.generic.router"; }
};

} // namespace Router
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
