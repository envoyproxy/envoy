#include "test/tools/schema_validator/validator.h"

#include "envoy/api/v2/rds.pb.h"
#include "envoy/api/v2/rds.pb.validate.h"

#include "common/protobuf/utility.h"

#include "tclap/CmdLine.h"

namespace Envoy {

const std::string Schema::ROUTE = "route";

const std::string& Schema::toString(Type type) {
  switch (type) {
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
  } else {
    std::cerr << "error: unknown schema type '" << schema_type.getValue() << "'" << std::endl;
    exit(EXIT_FAILURE);
  }

  config_path_ = config_path.getValue();
}

void Validator::validate(const std::string& config_path, Schema::Type schema_type) {

  switch (schema_type) {
  case Schema::Type::Route: {
    // Construct a envoy::api::v2::RouteConfiguration to validate the Route configuration and
    // ignore the output since nothing will consume it.
    envoy::api::v2::RouteConfiguration route_config;
    MessageUtil::loadFromFile(config_path, route_config, *api_);
    MessageUtil::validate(route_config);
    break;
  }
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace Envoy
