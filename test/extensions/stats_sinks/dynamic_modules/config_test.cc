#include "envoy/extensions/stat_sinks/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/configuration.h"

#include "source/extensions/stat_sinks/dynamic_modules/config.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {
namespace {

using testing::NiceMock;

class DynamicModuleStatsSinkFactoryTest : public testing::Test {
public:
  DynamicModuleStatsSinkFactoryTest() {
    // Point the search path at the directory containing our test .so files.
    std::string shared_object_path =
        Extensions::DynamicModules::testSharedObjectPath("stat_sink_no_op", "c");
    std::string shared_object_dir =
        std::filesystem::path(shared_object_path).parent_path().string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);
  }

  DynamicModuleStatsSinkFactory factory_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

TEST_F(DynamicModuleStatsSinkFactoryTest, FactoryName) {
  EXPECT_EQ(DynamicModuleStatsSinkName, factory_.name());
  EXPECT_EQ("envoy.stat_sinks.dynamic_modules", factory_.name());
}

TEST_F(DynamicModuleStatsSinkFactoryTest, FactoryRegistration) {
  auto* factory = Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(
      "envoy.stat_sinks.dynamic_modules");
  EXPECT_NE(nullptr, factory);
  EXPECT_EQ("envoy.stat_sinks.dynamic_modules", factory->name());
}

TEST_F(DynamicModuleStatsSinkFactoryTest, CreateEmptyConfigProto) {
  auto proto = factory_.createEmptyConfigProto();
  EXPECT_NE(nullptr, proto);
  EXPECT_NE(
      nullptr,
      dynamic_cast<envoy::extensions::stat_sinks::dynamic_modules::v3::DynamicModuleStatsSink*>(
          proto.get()));
}

TEST_F(DynamicModuleStatsSinkFactoryTest, Category) {
  EXPECT_EQ("envoy.stats_sinks", factory_.category());
}

// Happy path with a module that loads, resolves all symbols, and returns non-null
// from on_stat_sink_config_new.
TEST_F(DynamicModuleStatsSinkFactoryTest, ValidConfigNameBased) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: stat_sink_no_op
  do_not_close: true
sink_name: test_sink
sink_config:
  "@type": type.googleapis.com/google.protobuf.StringValue
  value: test_config
)EOF";

  envoy::extensions::stat_sinks::dynamic_modules::v3::DynamicModuleStatsSink proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto sink_or_error = factory_.createStatsSink(proto_config, context_);
  ASSERT_TRUE(sink_or_error.ok()) << sink_or_error.status().message();
  EXPECT_NE(nullptr, sink_or_error.value());
}

// Happy path with explicit module.local.filename.
TEST_F(DynamicModuleStatsSinkFactoryTest, ValidConfigExplicitPath) {
  const std::string yaml =
      fmt::format(R"EOF(
dynamic_module_config:
  module:
    local:
      filename: {}
  do_not_close: true
sink_name: test_sink
)EOF",
                  Extensions::DynamicModules::testSharedObjectPath("stat_sink_no_op", "c"));

  envoy::extensions::stat_sinks::dynamic_modules::v3::DynamicModuleStatsSink proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto sink_or_error = factory_.createStatsSink(proto_config, context_);
  ASSERT_TRUE(sink_or_error.ok()) << sink_or_error.status().message();
  EXPECT_NE(nullptr, sink_or_error.value());
}

// An empty sink_config is allowed — module receives zero bytes.
TEST_F(DynamicModuleStatsSinkFactoryTest, ValidConfigEmptySinkConfig) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: stat_sink_no_op
  do_not_close: true
sink_name: test_sink
)EOF";

  envoy::extensions::stat_sinks::dynamic_modules::v3::DynamicModuleStatsSink proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto sink_or_error = factory_.createStatsSink(proto_config, context_);
  ASSERT_TRUE(sink_or_error.ok()) << sink_or_error.status().message();
  EXPECT_NE(nullptr, sink_or_error.value());
}

// A Struct config is JSON-serialized before being handed to the module.
TEST_F(DynamicModuleStatsSinkFactoryTest, ValidConfigStructSerializedToJson) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: stat_sink_no_op
  do_not_close: true
sink_name: test_sink
sink_config:
  "@type": type.googleapis.com/google.protobuf.Struct
  value:
    endpoint: "127.0.0.1:9000"
    timeout_ms: 1000
)EOF";

  envoy::extensions::stat_sinks::dynamic_modules::v3::DynamicModuleStatsSink proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto sink_or_error = factory_.createStatsSink(proto_config, context_);
  ASSERT_TRUE(sink_or_error.ok()) << sink_or_error.status().message();
}

// A config with neither name nor module is rejected.
TEST_F(DynamicModuleStatsSinkFactoryTest, MissingBothNameAndModule) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  do_not_close: true
sink_name: test_sink
)EOF";

  envoy::extensions::stat_sinks::dynamic_modules::v3::DynamicModuleStatsSink proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto sink_or_error = factory_.createStatsSink(proto_config, context_);
  EXPECT_FALSE(sink_or_error.ok());
  EXPECT_THAT(std::string(sink_or_error.status().message()),
              testing::HasSubstr("Either 'name' or 'module' must be specified"));
}

// Remote modules are not supported for stats sinks.
TEST_F(DynamicModuleStatsSinkFactoryTest, RemoteModuleRejected) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  module:
    remote:
      http_uri:
        uri: https://example.com/module.so
        cluster: module_cluster
        timeout: 1s
      sha256: "0000000000000000000000000000000000000000000000000000000000000000"
sink_name: test_sink
)EOF";

  envoy::extensions::stat_sinks::dynamic_modules::v3::DynamicModuleStatsSink proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto sink_or_error = factory_.createStatsSink(proto_config, context_);
  EXPECT_FALSE(sink_or_error.ok());
  EXPECT_THAT(std::string(sink_or_error.status().message()),
              testing::HasSubstr("Remote module sources are not supported"));
}

// module.local with no filename: PGV (proto validation via downcastAndValidate)
// rejects this before our factory's explicit check runs, throwing an
// EnvoyException. Either path is acceptable — the proto is invalid.
TEST_F(DynamicModuleStatsSinkFactoryTest, ModuleLocalWithoutFilename) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  module:
    local: {}
sink_name: test_sink
)EOF";

  envoy::extensions::stat_sinks::dynamic_modules::v3::DynamicModuleStatsSink proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  // createStatsSink returns a [[no_discard]] absl::StatusOr<>, but PGV throws
  // before it would return; wrap in a lambda so we can silence the warning,
  // and so the comma inside the argument list isn't seen as a macro separator.
  auto invoke = [this, &proto_config]() {
    auto unused = factory_.createStatsSink(proto_config, context_);
    (void)unused;
  };
  EXPECT_THROW_WITH_REGEX(invoke(), EnvoyException, "specifier");
}

// A bogus module name produces a clear "Failed to load dynamic module" error.
TEST_F(DynamicModuleStatsSinkFactoryTest, InvalidModule) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: nonexistent_module_that_does_not_exist
sink_name: test_sink
)EOF";

  envoy::extensions::stat_sinks::dynamic_modules::v3::DynamicModuleStatsSink proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto sink_or_error = factory_.createStatsSink(proto_config, context_);
  EXPECT_FALSE(sink_or_error.ok());
  EXPECT_THAT(std::string(sink_or_error.status().message()),
              testing::HasSubstr("Failed to load dynamic module"));
}

// Module loaded OK but its on_stat_sink_config_new returns nullptr.
TEST_F(DynamicModuleStatsSinkFactoryTest, ConfigNewReturnsNull) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: stat_sink_config_new_fail
  do_not_close: true
sink_name: test_sink
)EOF";

  envoy::extensions::stat_sinks::dynamic_modules::v3::DynamicModuleStatsSink proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto sink_or_error = factory_.createStatsSink(proto_config, context_);
  EXPECT_FALSE(sink_or_error.ok());
  EXPECT_THAT(std::string(sink_or_error.status().message()),
              testing::HasSubstr("Failed to initialize dynamic module stats sink config"));
}

// Each of the four required symbols is missing in turn; each produces a clear
// symbol-resolution error.
TEST_F(DynamicModuleStatsSinkFactoryTest, MissingConfigNew) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: stat_sink_missing_config_new
  do_not_close: true
sink_name: test_sink
)EOF";

  envoy::extensions::stat_sinks::dynamic_modules::v3::DynamicModuleStatsSink proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto sink_or_error = factory_.createStatsSink(proto_config, context_);
  EXPECT_FALSE(sink_or_error.ok());
  EXPECT_THAT(std::string(sink_or_error.status().message()), testing::ContainsRegex("config_new"));
}

TEST_F(DynamicModuleStatsSinkFactoryTest, MissingConfigDestroy) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: stat_sink_missing_config_destroy
  do_not_close: true
sink_name: test_sink
)EOF";

  envoy::extensions::stat_sinks::dynamic_modules::v3::DynamicModuleStatsSink proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto sink_or_error = factory_.createStatsSink(proto_config, context_);
  EXPECT_FALSE(sink_or_error.ok());
  EXPECT_THAT(std::string(sink_or_error.status().message()),
              testing::ContainsRegex("config_destroy"));
}

TEST_F(DynamicModuleStatsSinkFactoryTest, MissingFlush) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: stat_sink_missing_flush
  do_not_close: true
sink_name: test_sink
)EOF";

  envoy::extensions::stat_sinks::dynamic_modules::v3::DynamicModuleStatsSink proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto sink_or_error = factory_.createStatsSink(proto_config, context_);
  EXPECT_FALSE(sink_or_error.ok());
  EXPECT_THAT(std::string(sink_or_error.status().message()), testing::ContainsRegex("flush"));
}

TEST_F(DynamicModuleStatsSinkFactoryTest, MissingHistogramComplete) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: stat_sink_missing_histogram_complete
  do_not_close: true
sink_name: test_sink
)EOF";

  envoy::extensions::stat_sinks::dynamic_modules::v3::DynamicModuleStatsSink proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto sink_or_error = factory_.createStatsSink(proto_config, context_);
  EXPECT_FALSE(sink_or_error.ok());
  EXPECT_THAT(std::string(sink_or_error.status().message()),
              testing::ContainsRegex("histogram_complete"));
}

} // namespace
} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
