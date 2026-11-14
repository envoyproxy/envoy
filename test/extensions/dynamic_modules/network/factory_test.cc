#include "envoy/extensions/filters/network/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/stats/custom_stat_namespaces_impl.h"
#include "source/extensions/filters/network/dynamic_modules/factory.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Server {
namespace Configuration {

using ::Envoy::StatusHelpers::HasStatusMessage;
using ::Envoy::StatusHelpers::IsOk;
using ::testing::Not;

class DynamicModuleNetworkFilterFactoryTest : public testing::Test {
public:
  void SetUp() override {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(
                                   "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
                               1);
  }

  NiceMock<MockFactoryContext> context_;
  DynamicModuleNetworkFilterConfigFactory factory_;
};

// Pull the shared dynamic-modules test helper into scope.
using ::Envoy::Extensions::DynamicModules::failureCounter;

TEST_F(DynamicModuleNetworkFilterFactoryTest, ValidConfig) {
  envoy::extensions::filters::network::dynamic_modules::v3::DynamicModuleNetworkFilter config;
  config.mutable_dynamic_module_config()->set_name("network_no_op");
  config.set_filter_name("test_filter");

  auto result = factory_.createFilterFactoryFromProto(config, context_);
  EXPECT_OK(result);

  // The happy path emits no load-failure counters.
  auto& server_scope = context_.server_factory_context_.serverScope();
  EXPECT_EQ(0U, failureCounter(server_scope, "module_load_error", "test_filter"));
  EXPECT_EQ(0U, failureCounter(server_scope, "config_init_error", "test_filter"));
}

TEST_F(DynamicModuleNetworkFilterFactoryTest, ValidConfigWithAccessLogs) {
  envoy::extensions::filters::network::dynamic_modules::v3::DynamicModuleNetworkFilter config;
  TestUtility::loadFromYaml(R"EOF(
dynamic_module_config:
  name: network_no_op
filter_name: test_filter
access_log:
- name: envoy.access_loggers.stdout
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.access_loggers.stream.v3.StdoutAccessLog
    log_format:
      text_format_source:
        inline_string: "%ACCESS_LOG_TYPE%\n"
access_log_options:
  access_log_flush_interval: 1s
  flush_access_log_on_start: true
)EOF",
                            config);

  auto result = factory_.createFilterFactoryFromProto(config, context_);
  ASSERT_OK(result);

  // The filter is the access log handler of its connection.
  testing::StrictMock<Network::MockFilterManager> filter_manager;
  Network::FilterSharedPtr filter;
  AccessLog::InstanceSharedPtr access_log_handler;
  EXPECT_CALL(filter_manager, addFilter(testing::_)).WillOnce(testing::SaveArg<0>(&filter));
  EXPECT_CALL(filter_manager, addAccessLogHandler(testing::_))
      .WillOnce(testing::SaveArg<0>(&access_log_handler));
  result.value()(filter_manager);
  ASSERT_NE(nullptr, filter);
  EXPECT_EQ(dynamic_cast<AccessLog::Instance*>(filter.get()), access_log_handler.get());

  // The filter takes its stream ID from the server's random generator, writes the start record and
  // arms the flush timer with the configured interval.
  NiceMock<Event::MockDispatcher> worker_dispatcher{"worker_0"};
  NiceMock<Network::MockReadFilterCallbacks> read_callbacks;
  ON_CALL(read_callbacks.connection_, dispatcher())
      .WillByDefault(testing::ReturnRef(worker_dispatcher));
  auto* timer = new NiceMock<Event::MockTimer>(&worker_dispatcher);
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(1000), testing::_));
  EXPECT_CALL(context_.server_factory_context_.api_.random_, uuid());
  EXPECT_CALL(read_callbacks.connection_.stream_info_, setStreamIdProvider(testing::_));
  EXPECT_CALL(*context_.server_factory_context_.access_log_manager_.file_,
              write(absl::string_view("TcpConnectionStart\n")));
  filter->initializeReadFilterCallbacks(read_callbacks);
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
}

// QUIC connections never flush the access logs of their network filters, so a QUIC listener rejects
// them.
TEST_F(DynamicModuleNetworkFilterFactoryTest, AccessLogsRejectedOnQuicListener) {
  envoy::extensions::filters::network::dynamic_modules::v3::DynamicModuleNetworkFilter config;
  config.mutable_dynamic_module_config()->set_name("network_no_op");
  config.set_filter_name("test_filter");
  config.add_access_log()->set_name("envoy.access_loggers.stdout");
  ON_CALL(context_, isQuic()).WillByDefault(testing::Return(true));

  EXPECT_THAT(factory_.createFilterFactoryFromProto(config, context_),
              HasStatusMessage("access_log is not supported on QUIC listeners"));
  config.clear_access_log();
  EXPECT_OK(factory_.createFilterFactoryFromProto(config, context_));
}

// Load the module via the ``module.local.filename`` data source instead of by name.
TEST_F(DynamicModuleNetworkFilterFactoryTest, ValidConfigWithLocalFile) {
  envoy::extensions::filters::network::dynamic_modules::v3::DynamicModuleNetworkFilter config;
  config.mutable_dynamic_module_config()->mutable_module()->mutable_local()->set_filename(
      Extensions::DynamicModules::testSharedObjectPath("network_no_op", "c"));
  config.set_filter_name("test_filter");

  auto result = factory_.createFilterFactoryFromProto(config, context_);
  EXPECT_OK(result);
}

// Remote module sources are not supported for network filters (no init manager is wired up).
TEST_F(DynamicModuleNetworkFilterFactoryTest, RemoteSourceRejected) {
  envoy::extensions::filters::network::dynamic_modules::v3::DynamicModuleNetworkFilter config;
  auto* remote = config.mutable_dynamic_module_config()->mutable_module()->mutable_remote();
  remote->mutable_http_uri()->set_uri("https://example.com/module.so");
  remote->mutable_http_uri()->set_cluster("cluster_1");
  remote->mutable_http_uri()->mutable_timeout()->set_seconds(5);
  remote->set_sha256("abc123");
  config.set_filter_name("test_filter");

  auto result = factory_.createFilterFactoryFromProto(config, context_);
  EXPECT_THAT(result, Not(IsOk()));
}

TEST_F(DynamicModuleNetworkFilterFactoryTest, ValidConfigWithFilterConfig) {
  envoy::extensions::filters::network::dynamic_modules::v3::DynamicModuleNetworkFilter config;
  config.mutable_dynamic_module_config()->set_name("network_no_op");
  config.set_filter_name("test_filter");
  std::ignore =
      config.mutable_filter_config()->PackFrom(ValueUtil::stringValue("test_config_value"));

  auto result = factory_.createFilterFactoryFromProto(config, context_);
  EXPECT_OK(result);
}

TEST_F(DynamicModuleNetworkFilterFactoryTest, InvalidModuleName) {
  envoy::extensions::filters::network::dynamic_modules::v3::DynamicModuleNetworkFilter config;
  config.mutable_dynamic_module_config()->set_name("nonexistent_module");
  config.set_filter_name("test_filter");

  auto result = factory_.createFilterFactoryFromProto(config, context_);
  EXPECT_THAT(result, HasStatusMessage(testing::HasSubstr("Failed to load dynamic module")));

  EXPECT_EQ(1U, failureCounter(context_.server_factory_context_.serverScope(), "module_load_error",
                               "test_filter"));
}

TEST_F(DynamicModuleNetworkFilterFactoryTest, MissingNetworkFilterSymbols) {
  // Use the HTTP-only no_op module which lacks network filter symbols.
  envoy::extensions::filters::network::dynamic_modules::v3::DynamicModuleNetworkFilter config;
  config.mutable_dynamic_module_config()->set_name("no_op");
  config.set_filter_name("test_filter");

  auto result = factory_.createFilterFactoryFromProto(config, context_);
  EXPECT_THAT(result, HasStatusMessage(testing::HasSubstr("Failed to create filter config")));

  // The module loads fine but lacks the network filter ABI symbols, so the failure is counted as
  // config_init_error, not module_load_error.
  auto& server_scope = context_.server_factory_context_.serverScope();
  EXPECT_EQ(1U, failureCounter(server_scope, "config_init_error", "test_filter"));
  EXPECT_EQ(0U, failureCounter(server_scope, "module_load_error", "test_filter"));
}

TEST_F(DynamicModuleNetworkFilterFactoryTest, ConfigInitializationFailure) {
  // Use a module that returns nullptr from config_new.
  envoy::extensions::filters::network::dynamic_modules::v3::DynamicModuleNetworkFilter config;
  config.mutable_dynamic_module_config()->set_name("network_config_new_fail");
  config.set_filter_name("test_filter");

  auto result = factory_.createFilterFactoryFromProto(config, context_);
  EXPECT_THAT(result, HasStatusMessage(testing::HasSubstr("Failed to create filter config")));

  EXPECT_EQ(1U, failureCounter(context_.server_factory_context_.serverScope(), "config_init_error",
                               "test_filter"));
}

TEST_F(DynamicModuleNetworkFilterFactoryTest, MalformedFilterConfig) {
  // The module loads fine, but the filter_config Any cannot be unpacked, so the failure is counted
  // as config_init_error (not module_load_error). A malformed Any must be built programmatically
  // because a filter_config parsed from YAML is always valid.
  envoy::extensions::filters::network::dynamic_modules::v3::DynamicModuleNetworkFilter config;
  config.mutable_dynamic_module_config()->set_name("network_no_op");
  config.set_filter_name("test_filter");
  auto* any = config.mutable_filter_config();
  any->set_type_url("type.googleapis.com/google.protobuf.StringValue");
  any->set_value("invalid_binary_data_that_cannot_be_unpacked_as_string_value");

  auto result = factory_.createFilterFactoryFromProto(config, context_);
  EXPECT_THAT(result, Not(IsOk()));

  auto& server_scope = context_.server_factory_context_.serverScope();
  EXPECT_EQ(1U, failureCounter(server_scope, "config_init_error", "test_filter"));
  EXPECT_EQ(0U, failureCounter(server_scope, "module_load_error", "test_filter"));
}

TEST_F(DynamicModuleNetworkFilterFactoryTest, FactoryName) {
  EXPECT_EQ("envoy.filters.network.dynamic_modules", factory_.name());
}

TEST_F(DynamicModuleNetworkFilterFactoryTest, IsTerminalFilterDefaultFalse) {
  envoy::extensions::filters::network::dynamic_modules::v3::DynamicModuleNetworkFilter config;
  NiceMock<MockServerFactoryContext> server_context;

  // Network dynamic modules are not terminal by default.
  EXPECT_FALSE(factory_.isTerminalFilterByProto(config, server_context));
}

TEST_F(DynamicModuleNetworkFilterFactoryTest, IsTerminalFilterExplicitTrue) {
  envoy::extensions::filters::network::dynamic_modules::v3::DynamicModuleNetworkFilter config;
  config.set_terminal_filter(true);
  NiceMock<MockServerFactoryContext> server_context;

  EXPECT_TRUE(factory_.isTerminalFilterByProto(config, server_context));
}

TEST_F(DynamicModuleNetworkFilterFactoryTest, ValidConfigWithTerminalFilter) {
  envoy::extensions::filters::network::dynamic_modules::v3::DynamicModuleNetworkFilter config;
  config.mutable_dynamic_module_config()->set_name("network_no_op");
  config.set_filter_name("test_filter");
  config.set_terminal_filter(true);

  // Terminal filter configuration should be accepted and create filter factory successfully.
  auto result = factory_.createFilterFactoryFromProto(config, context_);
  EXPECT_OK(result);

  // Verify the filter can still be added to filter manager.
  NiceMock<Network::MockFilterManager> filter_manager;
  EXPECT_CALL(filter_manager, addFilter(testing::_));
  result.value()(filter_manager);
}

TEST_F(DynamicModuleNetworkFilterFactoryTest, FilterFactoryCallbackAddsFilter) {
  envoy::extensions::filters::network::dynamic_modules::v3::DynamicModuleNetworkFilter config;
  config.mutable_dynamic_module_config()->set_name("network_no_op");
  config.set_filter_name("test_filter");

  auto result = factory_.createFilterFactoryFromProto(config, context_);
  ASSERT_OK(result);

  // Test that the filter factory callback correctly adds a filter.
  NiceMock<Network::MockFilterManager> filter_manager;
  EXPECT_CALL(filter_manager, addFilter(testing::_));
  EXPECT_CALL(filter_manager, addAccessLogHandler(testing::_)).Times(0);
  result.value()(filter_manager);
}

TEST_F(DynamicModuleNetworkFilterFactoryTest, DoNotCloseOption) {
  envoy::extensions::filters::network::dynamic_modules::v3::DynamicModuleNetworkFilter config;
  config.mutable_dynamic_module_config()->set_name("network_no_op");
  config.mutable_dynamic_module_config()->set_do_not_close(true);
  config.set_filter_name("test_filter");

  auto result = factory_.createFilterFactoryFromProto(config, context_);
  EXPECT_OK(result);
}

TEST_F(DynamicModuleNetworkFilterFactoryTest, LoadGloballyOption) {
  envoy::extensions::filters::network::dynamic_modules::v3::DynamicModuleNetworkFilter config;
  config.mutable_dynamic_module_config()->set_name("network_no_op");
  config.mutable_dynamic_module_config()->set_load_globally(true);
  config.set_filter_name("test_filter");

  auto result = factory_.createFilterFactoryFromProto(config, context_);
  EXPECT_OK(result);
}

// Test that the legacy behavior registers the custom stat namespace when the runtime guard is
// enabled.
TEST_F(DynamicModuleNetworkFilterFactoryTest, LegacyBehaviorWithRuntimeGuard) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix", "true"}});

  // Set up mock to expect the registerStatNamespace call.
  Stats::CustomStatNamespacesImpl custom_stat_namespaces;
  ON_CALL(context_.server_factory_context_.api_, customStatNamespaces())
      .WillByDefault(testing::ReturnRef(custom_stat_namespaces));

  envoy::extensions::filters::network::dynamic_modules::v3::DynamicModuleNetworkFilter config;
  config.mutable_dynamic_module_config()->set_name("network_no_op");
  config.mutable_dynamic_module_config()->set_metrics_namespace("custom_namespace");
  config.set_filter_name("test_filter");

  auto result = factory_.createFilterFactoryFromProto(config, context_);
  EXPECT_OK(result);

  // Verify the custom namespace was registered.
  EXPECT_TRUE(custom_stat_namespaces.registered("custom_namespace"));
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
