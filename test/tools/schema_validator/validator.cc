#include "test/tools/schema_validator/validator.h"

#include "common/router/config_impl.h"

#include "test/test_common/printers.h"

#include "tclap/CmdLine.h"

namespace Envoy {

const std::string Schema::ROUTE = "route";

const std::string& Schema::toString(Type type) {
  switch (type) {
  case Type::Route:
    return ROUTE;
  }

  NOT_REACHED;
}

Options::Options(int argc, char** argv) {
  TCLAP::CmdLine cmd("schema_validator_tool", ' ', "none", false);
  TCLAP::ValueArg<std::string> json_path("j", "json-path", "Path to JSON file.", true, "", "string",
                                         cmd);
  TCLAP::ValueArg<std::string> schema_type(
      "t", "schema-type",
      "Type of schema to validate the JSON against. Supported schema is: 'route'.", true, "",
      "string", cmd);

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

  json_path_ = json_path.getValue();
}

void Validator::validate(const std::string& json_path, Schema::Type schema_type) {
  Json::ObjectSharedPtr loader = Json::Factory::loadFromFile(json_path);

  switch (schema_type) {
  case Schema::Type::Route: {
    Runtime::MockLoader runtime;
    Upstream::MockClusterManager cm;
    // Construct a envoy::api::v2::RouteConfiguration to validate the Route configuration and
    // ignore the output since nothing will consume it.
    envoy::api::v2::RouteConfiguration route_config;
    Config::RdsJson::translateRouteConfiguration(*loader, route_config);
    break;
  }
  default:
    NOT_REACHED;
  }
}

} // namespace Envoy
