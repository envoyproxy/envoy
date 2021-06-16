#pragma once

#include <string>

#include "envoy/extensions/filters/network/mongo_proxy/v3/mongo_proxy.pb.h"
#include "envoy/extensions/filters/network/mongo_proxy/v3/mongo_proxy.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {

constexpr char MongoProxyName[] = "envoy.filters.network.mongo_proxy";

/**
 * Config registration for the mongo proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class MongoProxyFilterConfigFactory
    : public Common::FactoryBase<envoy::extensions::filters::network::mongo_proxy::v3::MongoProxy> {
public:
  MongoProxyFilterConfigFactory() : FactoryBase(MongoProxyName) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::mongo_proxy::v3::MongoProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
