#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/network/redis_proxy/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

TEST(RedisProxyFilterConfigFactoryTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(RedisProxyFilterConfigFactory().createFilterFactoryFromProto(
                   envoy::extensions::filters::network::redis_proxy::v3::RedisProxy(), context),
               ProtoValidationException);
}

TEST(RedisProxyFilterConfigFactoryTest, NoUpstreamDefined) {
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings settings;
  settings.mutable_op_timeout()->CopyFrom(Protobuf::util::TimeUtil::MillisecondsToDuration(20));

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy config;
  config.set_stat_prefix("foo");
  config.mutable_settings()->CopyFrom(settings);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_THROW_WITH_MESSAGE(
      RedisProxyFilterConfigFactory().createFilterFactoryFromProto(config, context), EnvoyException,
      "cannot configure a redis-proxy without any upstream");
}

TEST(RedisProxyFilterConfigFactoryTest, RedisProxyNoSettings) {
  const std::string yaml = R"EOF(
prefix_routes:
  catch_all_route:
    cluster: fake_cluster
stat_prefix: foo
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config;
  EXPECT_THROW_WITH_REGEX(TestUtility::loadFromYamlAndValidate(yaml, proto_config),
                          ProtoValidationException, "value is required");
}

TEST(RedisProxyFilterConfigFactoryTest, RedisProxyNoOpTimeout) {
  const std::string yaml = R"EOF(
prefix_routes:
  catch_all_route:
    cluster: fake_cluster
stat_prefix: foo
settings: {}
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config;
  EXPECT_THROW_WITH_REGEX(TestUtility::loadFromYamlAndValidate(yaml, proto_config),
                          ProtoValidationException, "embedded message failed validation");
}

TEST(RedisProxyFilterConfigFactoryTest,
     DEPRECATED_FEATURE_TEST(RedisProxyCorrectProtoLegacyCluster)) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.deprecated_features:envoy.config.filter.network.redis_proxy.v2.RedisProxy.cluster",
        "true"},
       {"envoy.deprecated_features:envoy.extensions.filters.network.redis_proxy.v3.RedisProxy."
        "hidden_envoy_deprecated_cluster",
        "true"}});

  const std::string yaml = R"EOF(
cluster: fake_cluster
stat_prefix: foo
settings:
  op_timeout: 0.02s
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config{};
  TestUtility::loadFromYamlAndValidate(yaml, proto_config, true, false);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RedisProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  EXPECT_TRUE(factory.isTerminalFilter());
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(RedisProxyFilterConfigFactoryTest,
     DEPRECATED_FEATURE_TEST(RedisProxyCorrectProtoLegacyCatchAllCluster)) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.deprecated_features:envoy.config.filter.network.redis_proxy.v2.RedisProxy."
        "PrefixRoutes.catch_all_cluster",
        "true"},
       {"envoy.deprecated_features:envoy.extensions.filters.network.redis_proxy.v3.RedisProxy."
        "PrefixRoutes.hidden_envoy_deprecated_catch_all_cluster",
        "true"}});
  const std::string yaml = R"EOF(
prefix_routes:
  catch_all_cluster: fake_cluster
stat_prefix: foo
settings:
  op_timeout: 0.02s
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config{};
  TestUtility::loadFromYamlAndValidate(yaml, proto_config, true, false);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RedisProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  EXPECT_TRUE(factory.isTerminalFilter());
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(RedisProxyFilterConfigFactoryTest, RedisProxyCorrectProto) {
  const std::string yaml = R"EOF(
prefix_routes:
  catch_all_route:
    cluster: fake_cluster
stat_prefix: foo
settings:
  op_timeout: 0.02s
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config{};
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RedisProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  EXPECT_TRUE(factory.isTerminalFilter());
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(RedisProxyFilterConfigFactoryTest, RedisProxyEmptyProto) {
  const std::string yaml = R"EOF(
prefix_routes:
  catch_all_route:
    cluster: fake_cluster
stat_prefix: foo
settings:
  op_timeout: 0.02s
  )EOF";

  NiceMock<Server::Configuration::MockFactoryContext> context;
  RedisProxyFilterConfigFactory factory;
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      *dynamic_cast<envoy::extensions::filters::network::redis_proxy::v3::RedisProxy*>(
          factory.createEmptyConfigProto().get());

  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(RedisProxyFilterConfigFactoryTest, RedisProxyFaultProto) {
  const std::string yaml = R"EOF(
prefix_routes:
  catch_all_route:
    cluster: fake_cluster
stat_prefix: foo
faults:
- fault_type: ERROR
  fault_enabled:
    default_value:
      numerator: 30
      denominator: HUNDRED
    runtime_key: "bogus_key"
  commands:
  - GET
- fault_type: DELAY
  fault_enabled:
    default_value:
      numerator: 20
      denominator: HUNDRED
    runtime_key: "bogus_key"
  delay: 2s
settings:
  op_timeout: 0.02s
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config{};
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RedisProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  EXPECT_TRUE(factory.isTerminalFilter());
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

// Test that the deprecated extension name still functions.
TEST(RedisProxyFilterConfigFactoryTest, DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  const std::string deprecated_name = "envoy.redis_proxy";

  ASSERT_NE(
      nullptr,
      Registry::FactoryRegistry<Server::Configuration::NamedNetworkFilterConfigFactory>::getFactory(
          deprecated_name));
}

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
