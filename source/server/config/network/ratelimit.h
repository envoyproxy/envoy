#pragma once

#include <string>

#include "envoy/config/filter/network/rate_limit/v2/rate_limit.pb.h"
#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the rate limit filter. @see NamedNetworkFilterConfigFactory.
 */
class RateLimitConfigFactory : public NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  NetworkFilterFactoryCb createFilterFactory(const Json::Object& json_config,
                                             FactoryContext& context) override;

  NetworkFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                      FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::config::filter::network::rate_limit::v2::RateLimit()};
  }

  std::string name() override { return Config::NetworkFilterNames::get().RATE_LIMIT; }

private:
  NetworkFilterFactoryCb
  createFilter(const envoy::config::filter::network::rate_limit::v2::RateLimit& proto_config,
               FactoryContext& context);
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
