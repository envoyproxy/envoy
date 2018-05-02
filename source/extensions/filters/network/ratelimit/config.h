#pragma once

#include "envoy/config/filter/network/rate_limit/v2/rate_limit.pb.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RateLimitFilter {

/**
 * Config registration for the rate limit filter. @see NamedNetworkFilterConfigFactory.
 */
class RateLimitConfigFactory : public Server::Configuration::NamedNetworkFilterConfigFactory {
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
        new envoy::config::filter::network::rate_limit::v2::RateLimit()};
  }

  std::string name() override { return NetworkFilterNames::get().RATE_LIMIT; }

private:
  Network::FilterFactoryCb
  createFilter(const envoy::config::filter::network::rate_limit::v2::RateLimit& proto_config,
               Server::Configuration::FactoryContext& context);
};

} // namespace RateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
