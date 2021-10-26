#pragma once

#include "contrib/generic_proxy/filters/network/source/filters/router/router.h"
#include "contrib/generic_proxy/filters/network/source/interface/generic_config.h"

namespace Envoy {
namespace Proxy {
namespace NetworkFilters {
namespace GenericProxy {
namespace Router {

class RouterFactory : public NamedGenericFilterConfigFactory {
public:
  FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config, const std::string& stat_prefix,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override { return nullptr; }

  bool isTerminalFilter() override { return true; }

  std::string name() const override { return "envoy.filters.generic.router"; }
};

} // namespace Router
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Proxy
} // namespace Envoy
