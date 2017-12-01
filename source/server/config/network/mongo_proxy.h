#pragma once

#include <string>

#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

#include "api/filter/network/mongo_proxy.pb.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the mongo proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class MongoProxyFilterConfigFactory : public NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  NetworkFilterFactoryCb createFilterFactory(const Json::Object& config,
                                             FactoryContext& context) override;
  NetworkFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& config,
                                                      FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::api::v2::filter::network::MongoProxy()};
  }

  std::string name() override { return Config::NetworkFilterNames::get().MONGO_PROXY; }

private:
  NetworkFilterFactoryCb
  createMongoProxyFactory(const envoy::api::v2::filter::network::MongoProxy& config,
                          FactoryContext& context);
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
