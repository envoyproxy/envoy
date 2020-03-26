#pragma once

#include <string>

#include "envoy/extensions/filters/network/mongo_proxy/v3/mongo_proxy.pb.h"
#include "envoy/extensions/filters/network/mongo_proxy/v3/mongo_proxy.pb.validate.h"

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
    : public Common::FactoryBase<envoy::extensions::filters::network::mongo_proxy::v3::MongoProxy> {
public:
  MongoProxyFilterConfigFactory() : FactoryBase(NetworkFilterNames::get().MongoProxy) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::mongo_proxy::v3::MongoProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
