#include "test/test_common/environment.h"
#include "test/tools/schema_validator/validator.h"

#include "gtest/gtest.h"

namespace Envoy {

class SchemaValidatorTest : public testing::Test {
public:
  using CommandLineFormatter = std::function<std::string(absl::string_view)>;
  void run(CommandLineFormatter command_line_formatter, const std::string& config_file) {
    const std::string final_command_line = TestEnvironment::runfilesPath(
        absl::StrCat("test/tools/schema_validator/test/config/", config_file));
    // Splitting on ' ' is not always reliable but works fine for these tests.
    const std::vector<std::string> split_command_line =
        absl::StrSplit(command_line_formatter(final_command_line), ' ');
    std::vector<const char*> c_command_line;
    c_command_line.reserve(split_command_line.size());
    for (auto& part : split_command_line) {
      c_command_line.push_back(part.c_str());
    }

    Validator::run(c_command_line.size(), c_command_line.data());
  }
};

// Basic success case.
TEST_F(SchemaValidatorTest, LdsSuccess) {
  CommandLineFormatter formatter = [](absl::string_view final_command_line) {
    return fmt::format("schema_validator_tool -c {} -t discovery_response", final_command_line);
  };
  run(formatter, "lds.yaml");
}

// Basic success case with full checking.
TEST_F(SchemaValidatorTest, LdsDeprecationAndWiPSuccess) {
  CommandLineFormatter formatter = [](absl::string_view final_command_line) {
    return fmt::format(
        "schema_validator_tool -c {} -t discovery_response --fail-on-wip --fail-on-deprecated",
        final_command_line);
  };
  run(formatter, "lds.yaml");
}

// No errors without fail on deprecated.
TEST_F(SchemaValidatorTest, LdsSuccessWithoutFailOnDeprecated) {
  CommandLineFormatter formatter = [](absl::string_view final_command_line) {
    return fmt::format("schema_validator_tool -c {} -t discovery_response", final_command_line);
  };
  run(formatter, "lds_deprecated.yaml");
}

// Fail on deprecated.
TEST_F(SchemaValidatorTest, LdsFailOnDeprecated) {
  CommandLineFormatter formatter = [](absl::string_view final_command_line) {
    return fmt::format("schema_validator_tool -c {} -t discovery_response --fail-on-deprecated",
                       final_command_line);
  };
  EXPECT_THROW_WITH_REGEX(
      run(formatter, "lds_deprecated.yaml"), EnvoyException,
      "Using deprecated option 'envoy.config.listener.v3.FilterChain.use_proxy_proto' from file "
      "listener_components.proto");
}

// Unknown field.
TEST_F(SchemaValidatorTest, LdsUnknownField) {
  CommandLineFormatter formatter = [](absl::string_view final_command_line) {
    return fmt::format("schema_validator_tool -c {} -t discovery_response", final_command_line);
  };
  EXPECT_THROW(run(formatter, "lds_unknown.yaml"), EnvoyException);
}

// Invalid type struct URL cases.
TEST_F(SchemaValidatorTest, LdsInvalidTypedStruct) {
  CommandLineFormatter formatter = [](absl::string_view final_command_line) {
    return fmt::format("schema_validator_tool -c {} -t discovery_response", final_command_line);
  };
  EXPECT_THROW_WITH_REGEX(run(formatter, "lds_invalid_typed_struct.yaml"), EnvoyException,
                          "Invalid type_url 'blah' during traversal");

  EXPECT_THROW_WITH_REGEX(run(formatter, "lds_invalid_typed_struct_2.yaml"), EnvoyException,
                          "Invalid type_url 'bleh' during traversal");
}

// No errors without fail on WiP.
TEST_F(SchemaValidatorTest, LdsSuccessWithoutFailOnWiP) {
  CommandLineFormatter formatter = [](absl::string_view final_command_line) {
    return fmt::format("schema_validator_tool -c {} -t discovery_response", final_command_line);
  };
  run(formatter, "lds_wip.yaml");
}

// Fail on WiP.
TEST_F(SchemaValidatorTest, LdsFailOnWiP) {
  CommandLineFormatter formatter = [](absl::string_view final_command_line) {
    return fmt::format("schema_validator_tool -c {} -t discovery_response --fail-on-wip",
                       final_command_line);
  };
  EXPECT_THROW_WITH_REGEX(
      run(formatter, "lds_wip.yaml"), EnvoyException,
      "field 'envoy.config.core.v3.Http3ProtocolOptions.allow_extended_connect' is marked as "
      "work-in-progress");
}

// Basic success case.
TEST_F(SchemaValidatorTest, BootstrapSuccess) {
  CommandLineFormatter formatter = [](absl::string_view final_command_line) {
    return fmt::format("schema_validator_tool -c {} -t bootstrap", final_command_line);
  };
  run(formatter, "bootstrap.yaml");
}

// Bootstrap with PGV failure.
TEST_F(SchemaValidatorTest, BootstrapPgvFail) {
  CommandLineFormatter formatter = [](absl::string_view final_command_line) {
    return fmt::format("schema_validator_tool -c {} -t bootstrap", final_command_line);
  };
  EXPECT_THROW_WITH_REGEX(
      run(formatter, "bootstrap_pgv_fail.yaml"), EnvoyException,
      "Proto constraint validation failed \\(BootstrapValidationError.StatsFlushInterval: value "
      "must be inside range");
}

// LDS with PGV failure that requires recursing into an Any.
TEST_F(SchemaValidatorTest, LdsRecursivePgvFail) {
  CommandLineFormatter formatter = [](absl::string_view final_command_line) {
    return fmt::format("schema_validator_tool -c {} -t discovery_response", final_command_line);
  };
  EXPECT_THROW_WITH_REGEX(
      run(formatter, "lds_pgv_fail.yaml"), EnvoyException,
      "Proto constraint validation failed \\(HttpConnectionManagerValidationError.RouteConfig:"
      ".* caused by RouteConfigurationValidationError.VirtualHosts.*"
      "VirtualHostValidationError.Domains: value must contain at least 1 item\\(s\\)");
}

} // namespace Envoy
