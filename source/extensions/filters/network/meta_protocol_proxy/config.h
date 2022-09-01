
#pragma once

#include "envoy/extensions/filters/network/meta_protocol_proxy/v3/meta_protocol_proxy.pb.h"
#include "envoy/extensions/filters/network/meta_protocol_proxy/v3/meta_protocol_proxy.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/meta_protocol_proxy/proxy.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {

class Factory : public Envoy::Extensions::NetworkFilters::Common::FactoryBase<ProxyConfig> {
public:
  Factory() : FactoryBase(Filter::name(), true) {}

  Envoy::Network::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const ProxyConfig& proto_config,
                                    Envoy::Server::Configuration::FactoryContext& context) override;
};

} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
