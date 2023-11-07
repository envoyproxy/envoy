#include <memory>
#include <string>

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/network/redis_proxy/conn_pool_impl.h"
#include "source/extensions/filters/network/redis_proxy/router_impl.h"

#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/extensions/filters/network/redis_proxy/mocks.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Matcher;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::PrefixRoutes
createPrefixRoutes() {
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::PrefixRoutes prefix_routes;
  auto* routes = prefix_routes.mutable_routes();

  {
    auto* route = routes->Add();
    route->set_prefix("ab");
    route->set_cluster("fake_clusterA");
  }

  {
    auto* route = routes->Add();
    route->set_prefix("a");
    route->set_cluster("fake_clusterB");
  }

  return prefix_routes;
}

TEST(PrefixRoutesTest, RoutedToCatchAll) {
  auto upstream_c = std::make_shared<ConnPool::MockInstance>();

  Upstreams upstreams;
  upstreams.emplace("fake_clusterA", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("fake_clusterB", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("fake_clusterC", upstream_c);

  Runtime::MockLoader runtime_;

  auto prefix_routes = createPrefixRoutes();
  prefix_routes.mutable_catch_all_route()->set_cluster("fake_clusterC");

  PrefixRoutes router(prefix_routes, std::move(upstreams), runtime_);

  std::string key("c:bar");
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_EQ(upstream_c, router.upstreamPool(key, stream_info)->upstream(""));
}

TEST(PrefixRoutesTest, MissingCatchAll) {
  Upstreams upstreams;
  upstreams.emplace("fake_clusterA", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("fake_clusterB", std::make_shared<ConnPool::MockInstance>());

  Runtime::MockLoader runtime_;

  PrefixRoutes router(createPrefixRoutes(), std::move(upstreams), runtime_);

  std::string key("c:bar");
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_EQ(nullptr, router.upstreamPool(key, stream_info));
}

TEST(PrefixRoutesTest, RoutedToLongestPrefix) {
  auto upstream_a = std::make_shared<ConnPool::MockInstance>();

  Upstreams upstreams;
  upstreams.emplace("fake_clusterA", upstream_a);
  upstreams.emplace("fake_clusterB", std::make_shared<ConnPool::MockInstance>());

  Runtime::MockLoader runtime_;

  PrefixRoutes router(createPrefixRoutes(), std::move(upstreams), runtime_);

  std::string key("ab:bar");
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_EQ(upstream_a, router.upstreamPool(key, stream_info)->upstream(""));
}

TEST(PrefixRoutesTest, TestFormatterWithCatchAllRoute) {
  auto upstream_catch_all = std::make_shared<ConnPool::MockInstance>();
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks;
  NiceMock<Network::MockConnection> connection;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  TestEnvironment::setEnvVar("ENVOY_TEST_ENV", "catchAllEnv", 1);
  Envoy::Cleanup cleanup([]() { TestEnvironment::unsetEnvVar("ENVOY_TEST_ENV"); });
  const std::string format =
      "{%KEY%}-%ENVIRONMENT(ENVOY_TEST_ENV)%-%FILTER_STATE(redisKey)%-{%KEY%}";

  stream_info.filterState()->setData(
      "redisKey", std::make_unique<Envoy::Router::StringAccessorImpl>("subjectCN"),
      StreamInfo::FilterState::StateType::ReadOnly);

  ON_CALL(filter_callbacks, connection()).WillByDefault(ReturnRef(connection));
  ON_CALL(connection, streamInfo()).WillByDefault(ReturnRef(stream_info));

  Upstreams upstreams;
  upstreams.emplace("fake_clusterA", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("fake_clusterB", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("fake_catchAllCluster", upstream_catch_all);

  Runtime::MockLoader runtime_;
  auto prefix_routes = createPrefixRoutes();
  {
    auto* route = prefix_routes.mutable_catch_all_route();
    route->set_cluster("fake_catchAllCluster");
    route->set_remove_prefix(true);
    route->set_prefix("removeMe_");
    route->set_key_formatter(format);
  }
  PrefixRoutes router(prefix_routes, std::move(upstreams), runtime_);
  std::string key("removeMe_catchAllKey");
  EXPECT_EQ(upstream_catch_all,
            router.upstreamPool(key, filter_callbacks.connection().streamInfo())->upstream(""));
  EXPECT_EQ("{catchAllKey}-catchAllEnv-subjectCN-{catchAllKey}", key);
}

TEST(PrefixRoutesTest, TestFormatterWithPrefixRoute) {
  auto upstream_c = std::make_shared<ConnPool::MockInstance>();
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks;
  NiceMock<Network::MockConnection> connection;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  TestEnvironment::setEnvVar("ENVOY_TEST_ENV", "test", 1);
  Envoy::Cleanup cleanup([]() { TestEnvironment::unsetEnvVar("ENVOY_TEST_ENV"); });
  const std::string format =
      "{%KEY%}-%ENVIRONMENT(ENVOY_TEST_ENV)%-%FILTER_STATE(redisKey)%-{%KEY%}";

  stream_info.filterState()->setData(
      "redisKey", std::make_unique<Envoy::Router::StringAccessorImpl>("subjectCN"),
      StreamInfo::FilterState::StateType::ReadOnly);

  ON_CALL(filter_callbacks, connection()).WillByDefault(ReturnRef(connection));
  ON_CALL(connection, streamInfo()).WillByDefault(ReturnRef(stream_info));

  Upstreams upstreams;
  upstreams.emplace("fake_clusterA", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("fake_clusterB", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("fake_clusterC", upstream_c);

  Runtime::MockLoader runtime_;
  auto prefix_routes = createPrefixRoutes();
  {
    auto* route = prefix_routes.mutable_routes()->Add();
    route->set_prefix("abc");
    route->set_cluster("fake_clusterC");
    route->set_key_formatter(format);
  }
  PrefixRoutes router(prefix_routes, std::move(upstreams), runtime_);
  std::string key("abc:bar");
  EXPECT_EQ(upstream_c,
            router.upstreamPool(key, filter_callbacks.connection().streamInfo())->upstream(""));
  EXPECT_EQ("{abc:bar}-test-subjectCN-{abc:bar}", key);
}

TEST(PrefixRoutesTest, TestFormatterWithPercentInKey) {
  auto upstream_c = std::make_shared<ConnPool::MockInstance>();
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks;
  NiceMock<Network::MockConnection> connection;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  TestEnvironment::setEnvVar("ENVOY_TEST_ENV", "test", 1);
  Envoy::Cleanup cleanup([]() { TestEnvironment::unsetEnvVar("ENVOY_TEST_ENV"); });
  const std::string format =
      "{%KEY%}-%ENVIRONMENT(ENVOY_TEST_ENV)%-%FILTER_STATE(redisKey)%-{%KEY%}";

  stream_info.filterState()->setData(
      "redisKey", std::make_unique<Envoy::Router::StringAccessorImpl>("subjectCN"),
      StreamInfo::FilterState::StateType::ReadOnly);

  ON_CALL(filter_callbacks, connection()).WillByDefault(ReturnRef(connection));
  ON_CALL(connection, streamInfo()).WillByDefault(ReturnRef(stream_info));

  Upstreams upstreams;
  upstreams.emplace("fake_clusterA", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("fake_clusterB", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("fake_clusterC", upstream_c);

  Runtime::MockLoader runtime_;
  auto prefix_routes = createPrefixRoutes();
  {
    auto* route = prefix_routes.mutable_routes()->Add();
    route->set_prefix("");
    route->set_cluster("fake_clusterC");
    route->set_key_formatter(format);
  }
  PrefixRoutes router(prefix_routes, std::move(upstreams), runtime_);
  std::string key("%abc:bar%");
  EXPECT_EQ(upstream_c,
            router.upstreamPool(key, filter_callbacks.connection().streamInfo())->upstream(""));
  EXPECT_EQ("{%abc:bar%}-test-subjectCN-{%abc:bar%}", key);
}

TEST(PrefixRoutesTest, TestKeyPrefixFormatterWithMissingFilterState) {
  auto upstream_c = std::make_shared<ConnPool::MockInstance>();
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks;
  NiceMock<Network::MockConnection> connection;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  const std::string format = "%FILTER_STATE(redisKey)%";

  stream_info.filterState()->setData(
      "randomKey", std::make_unique<Envoy::Router::StringAccessorImpl>("test_value"),
      StreamInfo::FilterState::StateType::ReadOnly);

  ON_CALL(filter_callbacks, connection()).WillByDefault(ReturnRef(connection));
  ON_CALL(connection, streamInfo()).WillByDefault(ReturnRef(stream_info));

  Upstreams upstreams;
  upstreams.emplace("fake_clusterA", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("fake_clusterB", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("fake_clusterC", upstream_c);

  Runtime::MockLoader runtime_;
  auto prefix_routes = createPrefixRoutes();
  {
    auto* route = prefix_routes.mutable_routes()->Add();
    route->set_prefix("abc");
    route->set_cluster("fake_clusterC");
    route->set_key_formatter(format);
  }
  PrefixRoutes router(prefix_routes, std::move(upstreams), runtime_);
  std::string key("abc:bar");
  EXPECT_EQ(upstream_c,
            router.upstreamPool(key, filter_callbacks.connection().streamInfo())->upstream(""));
  EXPECT_EQ("abc:bar", key);
}

TEST(PrefixRoutesTest, CaseUnsensitivePrefix) {
  auto upstream_a = std::make_shared<ConnPool::MockInstance>();

  Upstreams upstreams;
  upstreams.emplace("fake_clusterA", upstream_a);
  upstreams.emplace("fake_clusterB", std::make_shared<ConnPool::MockInstance>());

  Runtime::MockLoader runtime_;

  auto prefix_routes = createPrefixRoutes();
  prefix_routes.set_case_insensitive(true);

  PrefixRoutes router(prefix_routes, std::move(upstreams), runtime_);

  std::string key("AB:bar");
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_EQ(upstream_a, router.upstreamPool(key, stream_info)->upstream(""));
}

TEST(PrefixRoutesTest, RemovePrefix) {
  auto upstream_a = std::make_shared<ConnPool::MockInstance>();

  Upstreams upstreams;
  upstreams.emplace("fake_clusterA", upstream_a);
  upstreams.emplace("fake_clusterB", std::make_shared<ConnPool::MockInstance>());

  Runtime::MockLoader runtime_;

  auto prefix_routes = createPrefixRoutes();

  {
    auto* route = prefix_routes.mutable_routes()->Add();
    route->set_prefix("abc");
    route->set_cluster("fake_clusterA");
    route->set_remove_prefix(true);
  }

  PrefixRoutes router(prefix_routes, std::move(upstreams), runtime_);

  std::string key("abc:bar");
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_EQ(upstream_a, router.upstreamPool(key, stream_info)->upstream(""));
  EXPECT_EQ(":bar", key);
}

TEST(PrefixRoutesTest, RoutedToShortestPrefix) {
  auto upstream_b = std::make_shared<ConnPool::MockInstance>();

  Upstreams upstreams;
  upstreams.emplace("fake_clusterA", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("fake_clusterB", upstream_b);

  Runtime::MockLoader runtime_;

  PrefixRoutes router(createPrefixRoutes(), std::move(upstreams), runtime_);

  std::string key("a:bar");
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_EQ(upstream_b, router.upstreamPool(key, stream_info)->upstream(""));
  EXPECT_EQ("a:bar", key);
}

TEST(PrefixRoutesTest, DifferentPrefixesSameUpstream) {
  auto upstream_b = std::make_shared<ConnPool::MockInstance>();

  Upstreams upstreams;
  upstreams.emplace("fake_clusterA", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("fake_clusterB", upstream_b);

  Runtime::MockLoader runtime_;

  auto prefix_routes = createPrefixRoutes();

  {
    auto* route = prefix_routes.mutable_routes()->Add();
    route->set_prefix("also_route_to_b");
    route->set_cluster("fake_clusterB");
  }

  PrefixRoutes router(prefix_routes, std::move(upstreams), runtime_);

  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  std::string key1("a:bar");
  EXPECT_EQ(upstream_b, router.upstreamPool(key1, stream_info)->upstream(""));

  std::string key2("also_route_to_b:bar");
  EXPECT_EQ(upstream_b, router.upstreamPool(key2, stream_info)->upstream(""));
}

TEST(PrefixRoutesTest, DuplicatePrefix) {
  Upstreams upstreams;
  upstreams.emplace("fake_clusterA", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("fake_clusterB", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("this_will_throw", std::make_shared<ConnPool::MockInstance>());

  Runtime::MockLoader runtime_;

  auto prefix_routes = createPrefixRoutes();

  {
    auto* route = prefix_routes.mutable_routes()->Add();
    route->set_prefix("ab");
    route->set_cluster("this_will_throw");
  }

  EXPECT_THROW_WITH_MESSAGE(PrefixRoutes router(prefix_routes, std::move(upstreams), runtime_),
                            EnvoyException, "prefix `ab` already exists.")
}

TEST(PrefixRoutesTest, RouteReadWriteToDiffClusters) {
  auto upstream_a = std::make_shared<ConnPool::MockInstance>();
  auto upstream_b = std::make_shared<ConnPool::MockInstance>();
  auto upstream_c = std::make_shared<ConnPool::MockInstance>();
  auto upstream_cr = std::make_shared<ConnPool::MockInstance>();

  Upstreams upstreams;
  upstreams.emplace("fake_clusterA", upstream_a);
  upstreams.emplace("fake_clusterB", upstream_b);
  upstreams.emplace("fake_clusterC", upstream_c);
  upstreams.emplace("fake_clusterCR", upstream_cr);

  Runtime::MockLoader runtime_;

  auto prefix_routes = createPrefixRoutes();
  prefix_routes.mutable_catch_all_route()->set_cluster("fake_clusterC");
  auto read_policy = prefix_routes.mutable_catch_all_route()->mutable_read_command_policy();
  read_policy->set_cluster("fake_clusterCR");

  PrefixRoutes router(prefix_routes, std::move(upstreams), runtime_);
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  std::string get_command("GET");
  std::string set_command("SET");

  std::string key1("c:bar");
  EXPECT_EQ(upstream_cr, router.upstreamPool(key1, stream_info)->upstream(get_command));
  EXPECT_EQ(upstream_c, router.upstreamPool(key1, stream_info)->upstream(set_command));

  std::string key2("ab:bar");
  EXPECT_EQ(upstream_a, router.upstreamPool(key2, stream_info)->upstream(get_command));
  EXPECT_EQ(upstream_a, router.upstreamPool(key2, stream_info)->upstream(set_command));
}

TEST(MirrorPolicyImplTest, ShouldMirrorDefault) {
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::PrefixRoutes::Route::
      RequestMirrorPolicy config;
  auto upstream = std::make_shared<ConnPool::MockInstance>();
  NiceMock<Runtime::MockLoader> runtime;

  MirrorPolicyImpl policy(config, upstream, runtime);

  EXPECT_EQ(true, policy.shouldMirror("get"));
  EXPECT_EQ(true, policy.shouldMirror("set"));
  EXPECT_EQ(true, policy.shouldMirror("GET"));
  EXPECT_EQ(true, policy.shouldMirror("SET"));
}

TEST(MirrorPolicyImplTest, MissingUpstream) {
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::PrefixRoutes::Route::
      RequestMirrorPolicy config;
  NiceMock<Runtime::MockLoader> runtime;

  MirrorPolicyImpl policy(config, nullptr, runtime);

  EXPECT_EQ(false, policy.shouldMirror("get"));
  EXPECT_EQ(false, policy.shouldMirror("set"));
  EXPECT_EQ(false, policy.shouldMirror("GET"));
  EXPECT_EQ(false, policy.shouldMirror("SET"));
}

TEST(MirrorPolicyImplTest, ExcludeReadCommands) {
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::PrefixRoutes::Route::
      RequestMirrorPolicy config;
  config.set_exclude_read_commands(true);
  auto upstream = std::make_shared<ConnPool::MockInstance>();
  NiceMock<Runtime::MockLoader> runtime;

  MirrorPolicyImpl policy(config, upstream, runtime);

  EXPECT_EQ(false, policy.shouldMirror("get"));
  EXPECT_EQ(true, policy.shouldMirror("set"));
  EXPECT_EQ(false, policy.shouldMirror("GET"));
  EXPECT_EQ(true, policy.shouldMirror("SET"));
}

TEST(MirrorPolicyImplTest, DefaultValueZero) {
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::PrefixRoutes::Route::
      RequestMirrorPolicy config;
  auto* runtime_fraction = config.mutable_runtime_fraction();
  auto* percentage = runtime_fraction->mutable_default_value();
  percentage->set_numerator(0);
  percentage->set_denominator(envoy::type::v3::FractionalPercent::HUNDRED);
  auto upstream = std::make_shared<ConnPool::MockInstance>();
  NiceMock<Runtime::MockLoader> runtime;

  MirrorPolicyImpl policy(config, upstream, runtime);

  EXPECT_EQ(false, policy.shouldMirror("get"));
  EXPECT_EQ(false, policy.shouldMirror("set"));
}

TEST(MirrorPolicyImplTest, DeterminedByRuntimeFraction) {
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::PrefixRoutes::Route::
      RequestMirrorPolicy config;
  auto* runtime_fraction = config.mutable_runtime_fraction();
  runtime_fraction->set_runtime_key("runtime_key");
  auto* percentage = runtime_fraction->mutable_default_value();
  percentage->set_numerator(50);
  percentage->set_denominator(envoy::type::v3::FractionalPercent::HUNDRED);
  auto upstream = std::make_shared<ConnPool::MockInstance>();

  NiceMock<Runtime::MockLoader> runtime;
  MirrorPolicyImpl policy(config, upstream, runtime);

  EXPECT_CALL(
      runtime.snapshot_,
      featureEnabled("runtime_key",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(50))))
      .Times(4)
      .WillRepeatedly(Return(true));
  EXPECT_EQ(true, policy.shouldMirror("get"));
  EXPECT_EQ(true, policy.shouldMirror("set"));
  EXPECT_EQ(true, policy.shouldMirror("GET"));
  EXPECT_EQ(true, policy.shouldMirror("SET"));

  EXPECT_CALL(
      runtime.snapshot_,
      featureEnabled("runtime_key",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(50))))
      .Times(4)
      .WillRepeatedly(Return(false));
  EXPECT_EQ(false, policy.shouldMirror("get"));
  EXPECT_EQ(false, policy.shouldMirror("set"));
  EXPECT_EQ(false, policy.shouldMirror("GET"));
  EXPECT_EQ(false, policy.shouldMirror("SET"));
}

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
