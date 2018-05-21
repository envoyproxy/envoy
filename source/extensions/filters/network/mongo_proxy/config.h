#pragma once

#include <string>

#include "envoy/config/filter/network/mongo_proxy/v2/mongo_proxy.pb.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {

/**
 * Config registration for the mongo proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class MongoProxyFilterConfigFactory
    : public Common::FactoryBase<envoy::config::filter::network::mongo_proxy::v2::MongoProxy> {
public:
  MongoProxyFilterConfigFactory() : FactoryBase(NetworkFilterNames::get().MONGO_PROXY) {}

  // NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb
  createFilterFactory(const Json::Object& proto_config,
                      Server::Configuration::FactoryContext& context) override;

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::network::mongo_proxy::v2::MongoProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
