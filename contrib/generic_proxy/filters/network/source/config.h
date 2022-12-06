
#pragma once

#include "envoy/registry/registry.h"

#include "source/extensions/filters/network/common/factory_base.h"

#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.validate.h"
#include "contrib/generic_proxy/filters/network/source/interface/config.h"
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

  static std::pair<CodecFactoryPtr, ProxyFactoryPtr>
  factoriesFromProto(const envoy::config::core::v3::TypedExtensionConfig& codec_config,
                     Server::Configuration::FactoryContext& context);

  static Rds::RouteConfigProviderSharedPtr
  routeConfigProviderFromProto(const ProxyConfig& config,
                               Server::Configuration::FactoryContext& context,
                               RouteConfigProviderManager& route_config_provider_manager);

  static std::vector<NamedFilterFactoryCb> filtersFactoryFromProto(
      const ProtobufWkt::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig>& filters,
      const std::string stats_prefix, Server::Configuration::FactoryContext& context);
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
