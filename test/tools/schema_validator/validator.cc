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
    exit(1);
  }

  if (schema_type.getValue() == Schema::toString(Schema::Type::Route)) {
    schema_type_ = Schema::Type::Route;
  } else {
    std::cerr << "error: unknown schema type '" << schema_type.getValue() << "'" << std::endl;
    exit(1);
  }

  json_path_ = json_path.getValue();
}

void Validator::validate(const std::string& json_path, Schema::Type schema_type) {
  Json::ObjectSharedPtr loader = Json::Factory::loadFromFile(json_path);

  if (schema_type == Schema::Type::Route) {
    // Construct a Router::ConfigImpl to validate the Route configuration.
    std::unique_ptr<NiceMock<Runtime::MockLoader>> runtime(new NiceMock<Runtime::MockLoader>());
    std::unique_ptr<NiceMock<Upstream::MockClusterManager>> cm(
        new NiceMock<Upstream::MockClusterManager>());
    static_cast<void>(Router::ConfigImpl(*loader, *runtime, *cm, false));
  }
}

} // Envoy