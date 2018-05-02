#pragma once

#include <string>

#include "envoy/config/filter/network/redis_proxy/v2/redis_proxy.pb.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

/**
 * Config registration for the redis proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class RedisProxyFilterConfigFactory
    : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb
  createFilterFactory(const Json::Object& json_config,
                      Server::Configuration::FactoryContext& context) override;
  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::config::filter::network::redis_proxy::v2::RedisProxy()};
  }

  std::string name() override { return NetworkFilterNames::get().REDIS_PROXY; }

private:
  Network::FilterFactoryCb
  createFilter(const envoy::config::filter::network::redis_proxy::v2::RedisProxy& proto_config,
               Server::Configuration::FactoryContext& context);
};

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
