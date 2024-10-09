#include "source/extensions/filters/http/local_ratelimit/config.h"
#include "source/extensions/filters/http/local_ratelimit/local_ratelimit.h"

#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/priority_set.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalRateLimitFilter {

TEST(Factory, GlobalEmptyConfig) {
  const std::string yaml = R"(
stat_prefix: test
  )";

  LocalRateLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_CALL(context.server_factory_context_.dispatcher_, createTimer_(_)).Times(0);
  auto callback = factory.createFilterFactoryFromProto(*proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  callback(filter_callback);
}

TEST(Factory, RouteSpecificFilterConfig) {
  const std::string config_yaml = R"(
stat_prefix: test
token_bucket:
  max_tokens: 1
  tokens_per_fill: 1
  fill_interval: 1000s
filter_enabled:
  runtime_key: test_enabled
  default_value:
    numerator: 100
    denominator: HUNDRED
filter_enforced:
  runtime_key: test_enforced
  default_value:
    numerator: 100
    denominator: HUNDRED
response_headers_to_add:
  - append_action: OVERWRITE_IF_EXISTS_OR_ADD
    header:
      key: x-test-rate-limit
      value: 'true'
  )";

  LocalRateLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  EXPECT_CALL(context.dispatcher_, createTimer_(_));
  const auto route_config = factory.createRouteSpecificFilterConfig(
      *proto_config, context, ProtobufMessage::getNullValidationVisitor());
  const auto* config = dynamic_cast<const FilterConfig*>(route_config.get());
  EXPECT_TRUE(config->requestAllowed({}).allowed);
}

TEST(Factory, EnabledEnforcedDisabledByDefault) {
  const std::string config_yaml = R"(
stat_prefix: test
token_bucket:
  max_tokens: 1
  tokens_per_fill: 1
  fill_interval: 1000s
  )";

  LocalRateLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  EXPECT_CALL(context.dispatcher_, createTimer_(_));
  const auto route_config = factory.createRouteSpecificFilterConfig(
      *proto_config, context, ProtobufMessage::getNullValidationVisitor());
  const auto* config = dynamic_cast<const FilterConfig*>(route_config.get());
  EXPECT_FALSE(config->enabled());
  EXPECT_FALSE(config->enforced());
}

TEST(Factory, PerRouteConfigNoTokenBucket) {
  const std::string config_yaml = R"(
stat_prefix: test
  )";

  LocalRateLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  EXPECT_THROW(factory.createRouteSpecificFilterConfig(*proto_config, context,
                                                       ProtobufMessage::getNullValidationVisitor()),
               EnvoyException);
}

TEST(Factory, FillTimerTooLow) {
  const std::string config_yaml = R"(
stat_prefix: test
token_bucket:
  max_tokens: 1
  tokens_per_fill: 1
  fill_interval: 0.040s
  )";

  LocalRateLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  EXPECT_CALL(context.dispatcher_, createTimer_(_));
  EXPECT_THROW(factory.createRouteSpecificFilterConfig(*proto_config, context,
                                                       ProtobufMessage::getNullValidationVisitor()),
               EnvoyException);
}

TEST(Factory, RouteSpecificFilterConfigWithDescriptorsWithNoTokenBucket) {
  const std::string config_yaml = R"(
stat_prefix: test
token_bucket:
  max_tokens: 1
  tokens_per_fill: 1
  fill_interval: 1000s
filter_enabled:
  runtime_key: test_enabled
  default_value:
    numerator: 100
    denominator: HUNDRED
filter_enforced:
  runtime_key: test_enforced
  default_value:
    numerator: 100
    denominator: HUNDRED
response_headers_to_add:
  - append_action: OVERWRITE_IF_EXISTS_OR_ADD
    header:
      key: x-test-rate-limit
      value: 'true'
descriptors:
- entries:
   - key: hello
     value: world
   - key: foo
     value: bar
- entries:
   - key: foo2
     value: bar2
  )";

  LocalRateLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  EXPECT_CALL(context.dispatcher_, createTimer_(_)).Times(0);
  EXPECT_THROW(factory.createRouteSpecificFilterConfig(*proto_config, context,
                                                       ProtobufMessage::getNullValidationVisitor()),
               EnvoyException);
}

TEST(Factory, RouteSpecificFilterConfigWithDescriptors) {
  const std::string config_yaml = R"(
stat_prefix: test
token_bucket:
  max_tokens: 1
  tokens_per_fill: 1
  fill_interval: 60s
filter_enabled:
  runtime_key: test_enabled
  default_value:
    numerator: 100
    denominator: HUNDRED
filter_enforced:
  runtime_key: test_enforced
  default_value:
    numerator: 100
    denominator: HUNDRED
response_headers_to_add:
  - append_action: OVERWRITE_IF_EXISTS_OR_ADD
    header:
      key: x-test-rate-limit
      value: 'true'
descriptors:
- entries:
  - key: hello
    value: world
  - key: foo
    value: bar
  token_bucket:
    max_tokens: 10
    tokens_per_fill: 10
    fill_interval: 60s
- entries:
  - key: foo2
    value: bar2
  token_bucket:
    max_tokens: 100
    tokens_per_fill: 100
    fill_interval: 3600s
  )";

  LocalRateLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  EXPECT_CALL(context.dispatcher_, createTimer_(_));
  const auto route_config = factory.createRouteSpecificFilterConfig(
      *proto_config, context, ProtobufMessage::getNullValidationVisitor());
  const auto* config = dynamic_cast<const FilterConfig*>(route_config.get());
  EXPECT_TRUE(config->requestAllowed({}).allowed);
}

TEST(Factory, RouteSpecificFilterConfigWithDescriptorsTimerNotDivisible) {
  const std::string config_yaml = R"(
stat_prefix: test
token_bucket:
  max_tokens: 1
  tokens_per_fill: 1
  fill_interval: 100s
filter_enabled:
  runtime_key: test_enabled
  default_value:
    numerator: 100
    denominator: HUNDRED
filter_enforced:
  runtime_key: test_enforced
  default_value:
    numerator: 100
    denominator: HUNDRED
response_headers_to_add:
  - append_action: OVERWRITE_IF_EXISTS_OR_ADD
    header:
      key: x-test-rate-limit
      value: 'true'
descriptors:
- entries:
  - key: hello
    value: world
  - key: foo
    value: bar
  token_bucket:
    max_tokens: 10
    tokens_per_fill: 10
    fill_interval: 1s
- entries:
  - key: foo2
    value: bar2
  token_bucket:
    max_tokens: 100
    tokens_per_fill: 100
    fill_interval: 86400s
  )";

  LocalRateLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  EXPECT_CALL(context.dispatcher_, createTimer_(_));
  EXPECT_THROW(factory.createRouteSpecificFilterConfig(*proto_config, context,
                                                       ProtobufMessage::getNullValidationVisitor()),
               EnvoyException);
}

TEST(Factory, NonexistingHeaderFormatter) {
  const std::string config_yaml = R"(
stat_prefix: test
token_bucket:
  max_tokens: 1
  tokens_per_fill: 1
  fill_interval: 1000s
filter_enabled:
  runtime_key: test_enabled
  default_value:
    numerator: 100
    denominator: HUNDRED
filter_enforced:
  runtime_key: test_enforced
  default_value:
    numerator: 100
    denominator: HUNDRED
response_headers_to_add:
  - header:
      key: original-req-id
      value: '%WRONG_FORMATTER(x-request-id)%'
  )";

  LocalRateLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  EXPECT_THROW(factory.createRouteSpecificFilterConfig(*proto_config, context,
                                                       ProtobufMessage::getNullValidationVisitor()),
               EnvoyException);
}

TEST(Factory, LocalClusterRateLimitAndLocalRateLimitPerDownstreamConnection) {
  const std::string config_yaml = R"(
stat_prefix: test
token_bucket:
  max_tokens: 1
  tokens_per_fill: 1
  fill_interval: 1000s
filter_enabled:
  runtime_key: test_enabled
  default_value:
    numerator: 100
    denominator: HUNDRED
filter_enforced:
  runtime_key: test_enforced
  default_value:
    numerator: 100
    denominator: HUNDRED
local_cluster_rate_limit: {}
local_rate_limit_per_downstream_connection: true
)";

  LocalRateLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  EXPECT_THROW_WITH_MESSAGE(
      factory.createRouteSpecificFilterConfig(*proto_config, context,
                                              ProtobufMessage::getNullValidationVisitor()),
      EnvoyException,
      "local_cluster_rate_limit is set and local_rate_limit_per_downstream_connection is set to "
      "true");
}

TEST(Factory, LocalClusterRateLimitAndWithoutLocalClusterName) {
  const std::string config_yaml = R"(
stat_prefix: test
token_bucket:
  max_tokens: 1
  tokens_per_fill: 1
  fill_interval: 1000s
filter_enabled:
  runtime_key: test_enabled
  default_value:
    numerator: 100
    denominator: HUNDRED
filter_enforced:
  runtime_key: test_enforced
  default_value:
    numerator: 100
    denominator: HUNDRED
local_cluster_rate_limit: {}
)";

  LocalRateLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  EXPECT_THROW_WITH_MESSAGE(
      factory.createRouteSpecificFilterConfig(*proto_config, context,
                                              ProtobufMessage::getNullValidationVisitor()),
      EnvoyException, "local_cluster_rate_limit is set but no local cluster name is present");
}

TEST(Factory, LocalClusterRateLimitAndWithoutLocalCluster) {
  const std::string config_yaml = R"(
stat_prefix: test
token_bucket:
  max_tokens: 1
  tokens_per_fill: 1
  fill_interval: 1000s
filter_enabled:
  runtime_key: test_enabled
  default_value:
    numerator: 100
    denominator: HUNDRED
filter_enforced:
  runtime_key: test_enforced
  default_value:
    numerator: 100
    denominator: HUNDRED
local_cluster_rate_limit: {}
)";

  LocalRateLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  context.cluster_manager_.local_cluster_name_ = "local_cluster";

  EXPECT_THROW_WITH_MESSAGE(
      factory.createRouteSpecificFilterConfig(*proto_config, context,
                                              ProtobufMessage::getNullValidationVisitor()),
      EnvoyException, "local_cluster_rate_limit is set but no local cluster is present");
}

TEST(Factory, LocalClusterRateLimit) {
  const std::string config_yaml = R"(
stat_prefix: test
token_bucket:
  max_tokens: 1
  tokens_per_fill: 1
  fill_interval: 1000s
filter_enabled:
  runtime_key: test_enabled
  default_value:
    numerator: 100
    denominator: HUNDRED
filter_enforced:
  runtime_key: test_enforced
  default_value:
    numerator: 100
    denominator: HUNDRED
local_cluster_rate_limit: {}
)";

  LocalRateLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  context.cluster_manager_.local_cluster_name_ = "local_cluster";
  context.cluster_manager_.initializeClusters({"local_cluster"}, {});

  NiceMock<Upstream::MockPrioritySet> priority_set;
  const auto* local_cluster = context.cluster_manager_.active_clusters_.at("local_cluster").get();
  EXPECT_CALL(*local_cluster, prioritySet()).WillOnce(ReturnRef(priority_set));

  EXPECT_CALL(context.dispatcher_, createTimer_(_));
  EXPECT_NO_THROW(factory.createRouteSpecificFilterConfig(
      *proto_config, context, ProtobufMessage::getNullValidationVisitor()));
}

} // namespace LocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
