#include "ratelimit.h"

#include "envoy/network/connection.h"

#include "common/filter/ratelimit.h"

namespace Server {
namespace Configuration {

const std::string RateLimitConfigFactory::RATELIMIT_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "properties":{
      "stat_prefix" : {"type" : "string"},
      "domain" : {"type" : "string"},
      "descriptors": {
        "type": "array",
        "items" : {
          "type" : "array" ,
          "items": {
            "type": "object",
            "properties": {
              "key" : {"type" : "string"},
              "value" : {"type" : "string"}
            },
            "additionalProperties": false
          }
        }
      }
    },
    "required": ["stat_prefix", "descriptors", "domain"],
    "additionalProperties": false
  }
  )EOF");

NetworkFilterFactoryCb
RateLimitConfigFactory::tryCreateFilterFactory(NetworkFilterType type, const std::string& name,
                                               const Json::Object& json_config,
                                               Server::Instance& server) {
  if (type != NetworkFilterType::Read || name != "ratelimit") {
    return nullptr;
  }

  json_config.validateSchema(RATELIMIT_SCHEMA);

  RateLimit::TcpFilter::ConfigPtr config(
      new RateLimit::TcpFilter::Config(json_config, server.stats(), server.runtime()));
  return [config, &server](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(Network::ReadFilterPtr{new RateLimit::TcpFilter::Instance(
        config, server.rateLimitClient(Optional<std::chrono::milliseconds>()))});
  };
}

/**
 * Static registration for the rate limit filter. @see RegisterNetworkFilterConfigFactory.
 */
static RegisterNetworkFilterConfigFactory<RateLimitConfigFactory> registered_;

} // Configuration
} // Server
