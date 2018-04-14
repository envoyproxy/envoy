#pragma once

#include <string>

#include "envoy/config/filter/network/mongo_proxy/v2/mongo_proxy.pb.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {

/**
 * Config registration for the mongo proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class MongoProxyFilterConfigFactory
    : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  Server::Configuration::NetworkFilterFactoryCb
  createFilterFactory(const Json::Object& proto_config,
                      Server::Configuration::FactoryContext& context) override;
  Server::Configuration::NetworkFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::config::filter::network::mongo_proxy::v2::MongoProxy()};
  }

  std::string name() override { return NetworkFilterNames::get().MONGO_PROXY; }

private:
  Server::Configuration::NetworkFilterFactoryCb
  createFilter(const envoy::config::filter::network::mongo_proxy::v2::MongoProxy& proto_config,
               Server::Configuration::FactoryContext& context);
};

} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
