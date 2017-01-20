#include "ratelimit.h"

#include "envoy/network/connection.h"

#include "common/filter/ratelimit.h"

namespace Server {
namespace Configuration {

const std::string RateLimitConfigFactory::RATELIMIT_SCHEMA(
    "{\n"
    "\t\"$schema\": \"http://json-schema.org/schema#\",\n"
    "  \"properties\":{\n"
    "    \"stat_prefix\" : {\"type\" : \"string\"},\n"
    "    \"domain\" : {\"type\" : \"string\"},\n"
    "    \"descriptors\": {\n"
    "    \t\"type\": \"array\", \n"
    "    \t\"items\" : { \n"
    "    \t\t\"type\" : \"array\" ,\n"
    "    \t\t\"items\": { \n"
    "    \t\t\t\"type\": \"object\", \n"
    "    \t\t\t\"properties\": { \n"
    "    \t\t\t\t\"key\" : { \"type\" : \"string\" }, \n"
    "    \t\t\t\t\"value\" : { \"type\" : \"string\" }\n"
    "    \t\t\t},\n"
    "          \"additionalProperties\": false\n"
    "\t\t\t\t}\n"
    "\t\t\t}\n"
    "\t\t}\n"
    "  },\n"
    "  \"required\": [\"stat_prefix\", \"descriptors\", \"domain\"],\n"
    "  \"additionalProperties\": false\n"
    "}\t");

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
