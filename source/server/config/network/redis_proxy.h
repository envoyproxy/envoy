#pragma once

#include <string>

#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

#include "api/filter/network/redis_proxy.pb.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the redis proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class RedisProxyFilterConfigFactory : public NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  NetworkFilterFactoryCb createFilterFactory(const Json::Object& config,
                                             FactoryContext& context) override;
  NetworkFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& config,
                                                      FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::api::v2::filter::network::RedisProxy()};
  }

  std::string name() override { return Config::NetworkFilterNames::get().REDIS_PROXY; }

private:
  NetworkFilterFactoryCb
  createRedisProxyFactory(const envoy::api::v2::filter::network::RedisProxy& config,
                          FactoryContext& context);
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
