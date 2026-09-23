#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"
#include "source/extensions/filters/http/local_ratelimit/config.h"
#include "source/extensions/filters/http/local_ratelimit/local_ratelimit.h"

#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalRateLimitFilter {

using StatusHelpers::HasStatus;
using StatusHelpers::IsOk;

TEST(Factory, GlobalEmptyConfig) {
  const std::string yaml = R"(
stat_prefix: test
  )";

  LocalRateLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

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

  const auto route_config =
      factory
          .createRouteSpecificFilterConfig(*proto_config, context,
                                           ProtobufMessage::getNullValidationVisitor())
          .value();
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

  const auto route_config =
      factory
          .createRouteSpecificFilterConfig(*proto_config, context,
                                           ProtobufMessage::getNullValidationVisitor())
          .value();
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
  EXPECT_THAT(factory.createRouteSpecificFilterConfig(*proto_config, context,
                                                      ProtobufMessage::getNullValidationVisitor()),
              HasStatus(absl::StatusCode::kInvalidArgument,
                        "local rate limit token bucket must be set for per filter configs"));
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

  EXPECT_THROW(factory
                   .createRouteSpecificFilterConfig(*proto_config, context,
                                                    ProtobufMessage::getNullValidationVisitor())
                   .value(),
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

  EXPECT_THROW(factory
                   .createRouteSpecificFilterConfig(*proto_config, context,
                                                    ProtobufMessage::getNullValidationVisitor())
                   .value(),
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

  const auto route_config =
      factory
          .createRouteSpecificFilterConfig(*proto_config, context,
                                           ProtobufMessage::getNullValidationVisitor())
          .value();
  const auto* config = dynamic_cast<const FilterConfig*>(route_config.get());
  EXPECT_TRUE(config->requestAllowed({}).allowed);
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

  EXPECT_THAT(factory.createRouteSpecificFilterConfig(*proto_config, context,
                                                      ProtobufMessage::getNullValidationVisitor()),
              Not(IsOk()));
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

  auto config_or = factory.createRouteSpecificFilterConfig(
      *proto_config, context, ProtobufMessage::getNullValidationVisitor());
  EXPECT_THAT(config_or, HasStatus(absl::StatusCode::kInvalidArgument,
                                   "local_cluster_rate_limit is set and "
                                   "local_rate_limit_per_downstream_connection is set to true"));
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

  auto config_or = factory.createRouteSpecificFilterConfig(
      *proto_config, context, ProtobufMessage::getNullValidationVisitor());
  EXPECT_THAT(config_or,
              HasStatus(absl::StatusCode::kInvalidArgument,
                        "local_cluster_rate_limit is set but no local cluster name is present"));
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

  auto config_or = factory.createRouteSpecificFilterConfig(
      *proto_config, context, ProtobufMessage::getNullValidationVisitor());
  EXPECT_THAT(config_or,
              HasStatus(absl::StatusCode::kInvalidArgument,
                        "local_cluster_rate_limit is set but no local cluster is present"));
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

  EXPECT_OK(factory.createRouteSpecificFilterConfig(*proto_config, context,
                                                    ProtobufMessage::getNullValidationVisitor()));
}

TEST(Factory, GlobalEmptyConfigWithServerContext) {
  const std::string yaml = R"(
stat_prefix: test
  )";

  LocalRateLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Server::Configuration::ExtraFactoryContext extra_context{context.messageValidationVisitor(),
                                                           "stats"};

  auto callback =
      factory.createHttpFilterFactoryFromProto(*proto_config, context, extra_context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  callback(filter_callback);
}

// A route level configuration hands both the rate limiter and the share provider manager to the
// main dispatcher when it is released, because a route configuration can be released on a worker
// thread and neither the rate limiter's fill timer nor the manager's cluster membership callback
// handle can be torn down there.
TEST(Factory, RouteSpecificConfigReleasesSharedStateOnTheMainThread) {
  const std::string config_yaml = R"(
stat_prefix: test
token_bucket:
  max_tokens: 1
  tokens_per_fill: 1
  fill_interval: 1000s
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

  auto config_or_error = factory.createRouteSpecificFilterConfig(
      *proto_config, context, ProtobufMessage::getNullValidationVisitor());
  ASSERT_OK(config_or_error.status());
  auto config = std::move(config_or_error.value());

  // The share provider manager is an unpinned singleton, so the singleton manager holds only a
  // weak reference and the configuration owns the last strong one.
  std::weak_ptr<Filters::Common::LocalRateLimit::ShareProviderManager> share_provider_manager =
      Filters::Common::LocalRateLimit::ShareProviderManager::singleton(
          context.mainThreadDispatcher(), context.clusterManager(), context.singletonManager());
  ASSERT_FALSE(share_provider_manager.expired());

  Event::PostCb posted;
  EXPECT_CALL(context.dispatcher_, post(_))
      .WillOnce([&posted](Event::PostCb callback) { posted = std::move(callback); })
      // The share provider manager in turn posts its cluster membership callback handle.
      .WillOnce([](Event::PostCb callback) { callback(); });
  config.reset();
  ASSERT_TRUE(posted != nullptr);
  EXPECT_FALSE(share_provider_manager.expired());

  // Running the posted callback releases both on the main thread.
  posted();
  EXPECT_TRUE(share_provider_manager.expired());
}

} // namespace LocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
