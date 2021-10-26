
#pragma once

#include "envoy/registry/registry.h"

#include "source/extensions/filters/network/common/factory_base.h"

#include "contrib/envoy/extensions/filters/network/generic_proxy/v3alpha/generic_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/generic_proxy/v3alpha/generic_proxy.pb.validate.h"
#include "contrib/generic_proxy/filters/network/source/generic_proxy.h"

namespace Envoy {
namespace Proxy {
namespace NetworkFilters {
namespace GenericProxy {

class Factory : public Envoy::Extensions::NetworkFilters::Common::FactoryBase<GenericProxyConfig> {
public:
  Factory() : FactoryBase(Filter::name()) {}

  Envoy::Network::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const GenericProxyConfig& proto_config,
                                    Envoy::Server::Configuration::FactoryContext& context) override;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Proxy
} // namespace Envoy
