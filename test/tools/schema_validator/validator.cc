#include "test/tools/schema_validator/validator.h"

#include "common/router/config_impl.h"

#include "test/test_common/printers.h"

#include "tclap/CmdLine.h"

namespace Envoy {
Options::Options(int argc, char** argv) {
  TCLAP::CmdLine cmd("schema_validator_tool", ' ', "none", false);
  TCLAP::ValueArg<std::string> json_path("j", "json-path", "Path to JSON file to validate schema.",
                                         true, "", "string", cmd);
  TCLAP::ValueArg<std::string> schema_type(
      "t", "schema-type",
      "Type of schema to validate the JSON against. 'route' is the only supported schema.", true,
      "", "string", cmd);

  try {
    cmd.parse(argc, argv);
  } catch (TCLAP::ArgException& e) {
    std::cerr << "error: " << e.error() << std::endl;
    exit(1);
  }

  if (schema_type.getValue() == "route") {
    schema_type_ = schema_type.getValue();
  } else {
    std::cerr << "error: unknown schema type '" << schema_type.getValue() << "'" << std::endl;
    exit(1);
  }
  json_path_ = json_path.getValue();
}

void Validator::validate(Options options) {
  std::string type = options.schemaType();
  std::string path = options.configPath();

  Json::ObjectSharedPtr loader = Json::Factory::loadFromFile(path);

  if (type == "route") {
    std::unique_ptr<NiceMock<Runtime::MockLoader>> runtime(new NiceMock<Runtime::MockLoader>());
    std::unique_ptr<NiceMock<Upstream::MockClusterManager>> cm(
        new NiceMock<Upstream::MockClusterManager>());
    static_cast<void>(Router::ConfigImpl(*loader, *runtime, *cm, false));
  } else {
    throw EnvoyException(fmt::format("unsupported validator type: '{}'", type));
  }
}
} // Envoy