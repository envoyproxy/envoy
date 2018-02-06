#pragma once

#include <string>

#include "envoy/config/filter/network/mongo_proxy/v2/mongo_proxy.pb.h"
#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the mongo proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class MongoProxyFilterConfigFactory : public NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  NetworkFilterFactoryCb createFilterFactory(const Json::Object& proto_config,
                                             FactoryContext& context) override;
  NetworkFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                      FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::config::filter::network::mongo_proxy::v2::MongoProxy()};
  }

  std::string name() override { return Config::NetworkFilterNames::get().MONGO_PROXY; }

private:
  NetworkFilterFactoryCb
  createFilter(const envoy::config::filter::network::mongo_proxy::v2::MongoProxy& proto_config,
               FactoryContext& context);
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
