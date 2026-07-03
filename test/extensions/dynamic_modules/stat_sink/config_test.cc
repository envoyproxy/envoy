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

// Pull the shared dynamic-modules test helper into scope.
using ::Envoy::Extensions::DynamicModules::failureCounter;

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

  // The happy path emits no load-failure counters.
  EXPECT_EQ(0U, failureCounter(context_.serverScope(), "module_load_error", "test_sink"));
  EXPECT_EQ(0U, failureCounter(context_.serverScope(), "config_init_error", "test_sink"));
}

// Load the module via the ``module.local.filename`` data source instead of by name.
TEST_F(DynamicModuleStatsSinkFactoryTest, ValidConfigLocalFile) {
  envoy::extensions::stat_sinks::dynamic_modules::v3::DynamicModuleStatsSink proto_config;
  proto_config.mutable_dynamic_module_config()->mutable_module()->mutable_local()->set_filename(
      Extensions::DynamicModules::testSharedObjectPath("stat_sink_no_op", "c"));
  proto_config.mutable_dynamic_module_config()->set_do_not_close(true);
  proto_config.set_sink_name("test_sink");

  auto sink_or_error = factory_.createStatsSink(proto_config, context_);
  ASSERT_TRUE(sink_or_error.ok()) << sink_or_error.status().message();
  EXPECT_NE(nullptr, sink_or_error.value());
}

// Remote module sources are not supported for stats sinks (no init manager is wired up).
TEST_F(DynamicModuleStatsSinkFactoryTest, RemoteSourceRejected) {
  envoy::extensions::stat_sinks::dynamic_modules::v3::DynamicModuleStatsSink proto_config;
  auto* remote = proto_config.mutable_dynamic_module_config()->mutable_module()->mutable_remote();
  remote->mutable_http_uri()->set_uri("https://example.com/module.so");
  remote->mutable_http_uri()->set_cluster("cluster_1");
  remote->mutable_http_uri()->mutable_timeout()->set_seconds(5);
  remote->set_sha256("abc123");
  proto_config.set_sink_name("test_sink");

  auto sink_or_error = factory_.createStatsSink(proto_config, context_);
  EXPECT_FALSE(sink_or_error.ok());
}

// An empty sink_config is allowed and the module receives zero bytes.
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

// A sink_config Any that claims to be a StringValue but carries truncated wire bytes makes
// knownAnyToBytes fail after the module loads, surfacing a clear parse error. This must be set
// programmatically because a sink_config parsed from YAML is always valid.
TEST_F(DynamicModuleStatsSinkFactoryTest, MalformedSinkConfig) {
  envoy::extensions::stat_sinks::dynamic_modules::v3::DynamicModuleStatsSink proto_config;
  proto_config.mutable_dynamic_module_config()->set_name("stat_sink_no_op");
  proto_config.mutable_dynamic_module_config()->set_do_not_close(true);
  proto_config.set_sink_name("test_sink");
  auto* any = proto_config.mutable_sink_config();
  any->set_type_url("type.googleapis.com/google.protobuf.StringValue");
  any->set_value("\x0a");

  auto sink_or_error = factory_.createStatsSink(proto_config, context_);
  EXPECT_FALSE(sink_or_error.ok());
  EXPECT_THAT(std::string(sink_or_error.status().message()),
              testing::HasSubstr("Failed to parse sink config"));

  EXPECT_EQ(1U, failureCounter(context_.serverScope(), "config_init_error", "test_sink"));
  EXPECT_EQ(0U, failureCounter(context_.serverScope(), "module_load_error", "test_sink"));
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

  EXPECT_EQ(1U, failureCounter(context_.serverScope(), "module_load_error", "test_sink"));
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

  // The module loads fine but its config creation fails, so this is counted as config_init_error.
  EXPECT_EQ(1U, failureCounter(context_.serverScope(), "config_init_error", "test_sink"));
  EXPECT_EQ(0U, failureCounter(context_.serverScope(), "module_load_error", "test_sink"));
}

// Each of the four required symbols is missing in turn. Each produces a clear
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
