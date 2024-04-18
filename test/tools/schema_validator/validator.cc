#include "test/tools/schema_validator/validator.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.validate.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route.pb.validate.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.validate.h"

#include "source/common/protobuf/utility.h"
#include "source/common/version/version.h"

#include "tclap/CmdLine.h"

namespace Envoy {

const std::string Schema::BOOTSTRAP = "bootstrap";
const std::string Schema::DISCOVERY_RESPONSE = "discovery_response";
const std::string Schema::ROUTE = "route";

const std::string& Schema::toString(Type type) {
  switch (type) {
  case Type::Bootstrap:
    return BOOTSTRAP;
  case Type::DiscoveryResponse:
    return DISCOVERY_RESPONSE;
  case Type::Route:
    return ROUTE;
  }

  PANIC("reached unexpected code");
}

Options::Options(int argc, const char* const* argv) {
  // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall)
  TCLAP::CmdLine cmd("schema_validator_tool", ' ', VersionInfo::version());
  TCLAP::ValueArg<std::string> config_path("c", "config-path", "Path to configuration file.", true,
                                           "", "string", cmd);
  TCLAP::ValueArg<std::string> schema_type(
      "t", "schema-type",
      "Type of schema to validate the configuration against. "
      "Supported schema is: 'route', 'discovery_response', 'bootstrap'.",
      true, "", "string", cmd);
  TCLAP::SwitchArg fail_on_deprecated("", "fail-on-deprecated",
                                      "Whether to fail if using deprecated fields.", cmd, false);
  TCLAP::SwitchArg fail_on_wip("", "fail-on-wip",
                               "Whether to fail if using work-in-progress fields.", cmd, false);

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
  } else if (schema_type.getValue() == Schema::toString(Schema::Type::Bootstrap)) {
    schema_type_ = Schema::Type::Bootstrap;
  } else {
    std::cerr << "error: unknown schema type '" << schema_type.getValue() << "'" << std::endl;
    exit(EXIT_FAILURE);
  }

  config_path_ = config_path.getValue();
  fail_on_deprecated_ = fail_on_deprecated.getValue();
  fail_on_wip_ = fail_on_wip.getValue();
}

namespace {
class Visitor : public ProtobufMessage::ValidationVisitor {
public:
  Visitor(const Options& options) : options_(options) {}

  // ProtobufMessage::ValidationVisitor
  absl::Status onUnknownField(absl::string_view description) override {
    return absl::InvalidArgumentError(
        absl::StrCat("Protobuf message (", description, ") has unknown fields"));
  }
  bool skipValidation() override { return false; }
  absl::Status onDeprecatedField(absl::string_view description, bool) override {
    if (options_.failOnDeprecated()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failing due to deprecated field: ", description));
    }
    return absl::OkStatus();
  }
  void onWorkInProgress(absl::string_view description) override {
    if (options_.failOnWip()) {
      throw EnvoyException(absl::StrCat("Failing due to work-in-progress field: ", description));
    }
  }
  OptRef<Runtime::Loader> runtime() override { return {}; }

private:
  const Options& options_;
};
} // namespace

void Validator::validate(const Options& options) {
  Visitor visitor(options);
  std::cerr << "Validating: " << options.configPath() << "\n";

  switch (options.schemaType()) {
  case Schema::Type::DiscoveryResponse: {
    envoy::service::discovery::v3::DiscoveryResponse discovery_response_config;
    TestUtility::loadFromFile(options.configPath(), discovery_response_config, *api_);
    MessageUtil::validate(discovery_response_config, visitor, true);
    break;
  }
  case Schema::Type::Route: {
    envoy::config::route::v3::RouteConfiguration route_config;
    TestUtility::loadFromFile(options.configPath(), route_config, *api_);
    MessageUtil::validate(route_config, visitor, true);
    break;
  }
  case Schema::Type::Bootstrap: {
    envoy::config::bootstrap::v3::Bootstrap bootstrap;
    TestUtility::loadFromFile(options.configPath(), bootstrap, *api_);
    MessageUtil::validate(bootstrap, visitor, true);
    break;
  }
  }
}

void Validator::run(int argc, const char* const* argv) {
  Options options(argc, argv); // NOLINT(clang-analyzer-optin.cplusplus.VirtualCall)
  Validator v;

  v.validate(options);
}

} // namespace Envoy
