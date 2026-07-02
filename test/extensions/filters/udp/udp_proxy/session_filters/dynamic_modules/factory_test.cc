#include "envoy/extensions/filters/udp/udp_proxy/session/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/stats/custom_stat_namespaces_impl.h"
#include "source/extensions/filters/udp/udp_proxy/session_filters/dynamic_modules/factory.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace DynamicModules {

using ::Envoy::Extensions::DynamicModules::failureCounter;
using testing::NiceMock;

class MockChainFactoryCallbacks : public Network::UdpSessionFilterChainFactoryCallbacks {
public:
  MOCK_METHOD(void, addReadFilter, (Network::UdpSessionReadFilterSharedPtr));
  MOCK_METHOD(void, addWriteFilter, (Network::UdpSessionWriteFilterSharedPtr));
  MOCK_METHOD(void, addFilter, (Network::UdpSessionFilterSharedPtr));
};

class DynamicModuleUdpSessionFilterFactoryTest : public testing::Test {
public:
  void SetUp() override {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(
                                   "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
                               1);
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  DynamicModuleUdpSessionFilterConfigFactory factory_;
};

TEST_F(DynamicModuleUdpSessionFilterFactoryTest, ValidConfig) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: udp_session_no_op
  do_not_close: true
filter_name: test_filter
filter_config:
  "@type": type.googleapis.com/google.protobuf.StringValue
  value: test_config
)EOF";
  FilterConfig proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto callback = factory_.createFilterFactoryFromProto(proto_config, context_);
  MockChainFactoryCallbacks callbacks;
  EXPECT_CALL(callbacks, addFilter(testing::_));
  callback(callbacks);
}

TEST_F(DynamicModuleUdpSessionFilterFactoryTest, ValidConfigWithLocalFile) {
  FilterConfig proto_config;
  proto_config.mutable_dynamic_module_config()->mutable_module()->mutable_local()->set_filename(
      Extensions::DynamicModules::testSharedObjectPath("udp_session_no_op", "c"));
  proto_config.mutable_dynamic_module_config()->set_do_not_close(true);
  proto_config.set_filter_name("test_filter");

  auto callback = factory_.createFilterFactoryFromProto(proto_config, context_);
  MockChainFactoryCallbacks callbacks;
  EXPECT_CALL(callbacks, addFilter(testing::_));
  callback(callbacks);
}

TEST_F(DynamicModuleUdpSessionFilterFactoryTest, EmptyProto) {
  EXPECT_NE(nullptr, factory_.createEmptyConfigProto());
}

TEST_F(DynamicModuleUdpSessionFilterFactoryTest, FactoryName) {
  EXPECT_EQ("envoy.filters.udp.session.dynamic_modules", factory_.name());
}

TEST_F(DynamicModuleUdpSessionFilterFactoryTest, InvalidModulePath) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: nonexistent_module
filter_name: test_filter
)EOF";
  FilterConfig proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  EXPECT_THROW_WITH_REGEX(factory_.createFilterFactoryFromProto(proto_config, context_),
                          EnvoyException, "Failed to load.*");
  EXPECT_EQ(1U, failureCounter(context_.server_factory_context_.serverScope(), "module_load_error",
                               "test_filter"));
}

TEST_F(DynamicModuleUdpSessionFilterFactoryTest, ModuleWithoutSessionSupport) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: no_op
filter_name: test_filter
)EOF";
  FilterConfig proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  EXPECT_THROW_WITH_MESSAGE(
      factory_.createFilterFactoryFromProto(proto_config, context_), EnvoyException,
      "Dynamic module does not support UDP session filters: Failed to "
      "resolve symbol envoy_dynamic_module_on_udp_session_filter_config_new");
}

TEST_F(DynamicModuleUdpSessionFilterFactoryTest, ConfigWithBytesValue) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: udp_session_no_op
  do_not_close: true
filter_name: test_filter
filter_config:
  "@type": type.googleapis.com/google.protobuf.BytesValue
  value: "aGVsbG8="
)EOF";
  FilterConfig proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto callback = factory_.createFilterFactoryFromProto(proto_config, context_);
  MockChainFactoryCallbacks callbacks;
  EXPECT_CALL(callbacks, addFilter(testing::_));
  callback(callbacks);
}

// When the runtime guard is enabled, the custom stat namespace is registered.
TEST_F(DynamicModuleUdpSessionFilterFactoryTest, LegacyBehaviorWithRuntimeGuard) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix", "true"}});

  Stats::CustomStatNamespacesImpl custom_stat_namespaces;
  ON_CALL(context_.server_factory_context_.api_, customStatNamespaces())
      .WillByDefault(testing::ReturnRef(custom_stat_namespaces));

  const std::string yaml = R"EOF(
dynamic_module_config:
  name: udp_session_no_op
  do_not_close: true
  metrics_namespace: custom_namespace
filter_name: test_filter
)EOF";
  FilterConfig proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  factory_.createFilterFactoryFromProto(proto_config, context_);
  EXPECT_TRUE(custom_stat_namespaces.registered("custom_namespace"));
}

} // namespace DynamicModules
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
