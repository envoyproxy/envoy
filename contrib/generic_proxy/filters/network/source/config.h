
#pragma once

#include "envoy/registry/registry.h"

#include "source/extensions/filters/network/common/factory_base.h"

#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.validate.h"
#include "contrib/generic_proxy/filters/network/source/proxy.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

class Factory : public Envoy::Extensions::NetworkFilters::Common::FactoryBase<ProxyConfig> {
public:
  Factory() : FactoryBase(Filter::name(), true) {}

  Envoy::Network::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const ProxyConfig& proto_config,
                                    Envoy::Server::Configuration::FactoryContext& context) override;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
