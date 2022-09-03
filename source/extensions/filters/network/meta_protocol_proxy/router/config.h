#pragma once

#include "envoy/extensions/filters/network/meta_protocol_proxy/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/meta_protocol_proxy/router/v3/router.pb.validate.h"

#include "source/extensions/filters/network/meta_protocol_proxy/interface/config.h"
#include "source/extensions/filters/network/meta_protocol_proxy/router/router.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {
namespace Router {

class RouterFactory : public NamedFilterConfigFactory {
public:
  FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config, const std::string& stat_prefix,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::filters::network::meta_protocol_proxy::router::v3::Router>();
  }
  ProtobufTypes::MessagePtr createEmptyRouteConfigProto() override { return nullptr; }
  RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfig(const Protobuf::Message&,
                                  Server::Configuration::ServerFactoryContext&,
                                  ProtobufMessage::ValidationVisitor&) override {
    return nullptr;
  }
  bool isTerminalFilter() override { return true; }

  std::string name() const override { return "envoy.filters.meta.router"; }
};

} // namespace Router
} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
