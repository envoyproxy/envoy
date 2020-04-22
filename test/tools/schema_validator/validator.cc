#include "test/tools/schema_validator/validator.h"

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route.pb.validate.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.validate.h"

#include "common/protobuf/utility.h"

#include "tclap/CmdLine.h"

namespace Envoy {

const std::string Schema::DISCOVERY_RESPONSE = "discovery_response";
const std::string Schema::ROUTE = "route";

const std::string& Schema::toString(Type type) {
  switch (type) {
  case Type::DiscoveryResponse:
    return DISCOVERY_RESPONSE;
  case Type::Route:
    return ROUTE;
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

Options::Options(int argc, char** argv) {
  TCLAP::CmdLine cmd("schema_validator_tool", ' ', "none", false);
  TCLAP::ValueArg<std::string> config_path("c", "config-path", "Path to configuration file.", true,
                                           "", "string", cmd);
  TCLAP::ValueArg<std::string> schema_type(
      "t", "schema-type",
      "Type of schema to validate the configuration against. Supported schema is: 'route'.", true,
      "", "string", cmd);

  try {
    cmd.parse(argc, argv);
  } catch (TCLAP::ArgException& e) {
    std::cerr << "error: " << e.error() << std::endl;
    exit(EXIT_FAILURE);
  }

  if (schema_type.getValue() == Schema::toString(Schema::Type::Route)) {
    schema_type_ = Schema::Type::Route;
  } else if (schema_type.getValue() == Schema::toString(Schema::Type::DiscoveryResponse)) {
    schema_type_ = Schema::Type::DiscoveryResponse;
  } else {
    std::cerr << "error: unknown schema type '" << schema_type.getValue() << "'" << std::endl;
    exit(EXIT_FAILURE);
  }

  config_path_ = config_path.getValue();
}

void Validator::validate(const std::string& config_path, Schema::Type schema_type) {

  switch (schema_type) {
  case Schema::Type::DiscoveryResponse: {
    envoy::service::discovery::v3::DiscoveryResponse discovery_response_config;
    TestUtility::loadFromFile(config_path, discovery_response_config, *api_);
    TestUtility::validate(discovery_response_config);
    break;
  }
  case Schema::Type::Route: {
    envoy::config::route::v3::RouteConfiguration route_config;
    TestUtility::loadFromFile(config_path, route_config, *api_);
    TestUtility::validate(route_config);
    break;
  }
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace Envoy
