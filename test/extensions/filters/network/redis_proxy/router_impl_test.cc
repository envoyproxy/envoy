#include <string>

#include "extensions/filters/network/redis_proxy/conn_pool_impl.h"
#include "extensions/filters/network/redis_proxy/router_impl.h"

#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/extensions/filters/network/redis_proxy/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::InSequence;
using testing::Return;
using testing::StrEq;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

envoy::config::filter::network::redis_proxy::v2::RedisProxy::PrefixRoutes createPrefixRoutes() {
  envoy::config::filter::network::redis_proxy::v2::RedisProxy::PrefixRoutes prefix_routes;
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

TEST(PrefixRoutesTest, MissingCatchAll) {
  Upstreams upstreams;
  upstreams.emplace("fake_clusterA", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("fake_clusterB", std::make_shared<ConnPool::MockInstance>());

  PrefixRoutes router(createPrefixRoutes(), std::move(upstreams));

  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolCallbacks callbacks;

  EXPECT_EQ(nullptr, router.makeRequest("c:bar", value, callbacks));
}

TEST(PrefixRoutesTest, RoutedToCatchAll) {
  auto upstream_c = std::make_shared<ConnPool::MockInstance>();

  Upstreams upstreams;
  upstreams.emplace("fake_clusterA", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("fake_clusterB", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("fake_clusterC", upstream_c);

  auto prefix_routes = createPrefixRoutes();
  prefix_routes.set_catch_all_cluster("fake_clusterC");

  EXPECT_CALL(*upstream_c, makeRequest(Eq("c:bar"), _, _));

  PrefixRoutes router(prefix_routes, std::move(upstreams));
  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolCallbacks callbacks;

  EXPECT_EQ(nullptr, router.makeRequest("c:bar", value, callbacks));
}

TEST(PrefixRoutesTest, RoutedToLongestPrefix) {
  auto upstream_a = std::make_shared<ConnPool::MockInstance>();

  Upstreams upstreams;
  upstreams.emplace("fake_clusterA", upstream_a);
  upstreams.emplace("fake_clusterB", std::make_shared<ConnPool::MockInstance>());

  EXPECT_CALL(*upstream_a, makeRequest(Eq("ab:bar"), _, _));

  PrefixRoutes router(createPrefixRoutes(), std::move(upstreams));
  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolCallbacks callbacks;

  EXPECT_EQ(nullptr, router.makeRequest("ab:bar", value, callbacks));
}

TEST(PrefixRoutesTest, CaseUnsensitivePrefix) {
  auto upstream_a = std::make_shared<ConnPool::MockInstance>();

  Upstreams upstreams;
  upstreams.emplace("fake_clusterA", upstream_a);
  upstreams.emplace("fake_clusterB", std::make_shared<ConnPool::MockInstance>());

  auto prefix_routes = createPrefixRoutes();
  prefix_routes.set_case_insensitive(true);

  EXPECT_CALL(*upstream_a, makeRequest(Eq("AB:bar"), _, _));

  PrefixRoutes router(prefix_routes, std::move(upstreams));
  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolCallbacks callbacks;

  EXPECT_EQ(nullptr, router.makeRequest("AB:bar", value, callbacks));
}

TEST(PrefixRoutesTest, RemovePrefix) {
  auto upstream_a = std::make_shared<ConnPool::MockInstance>();

  Upstreams upstreams;
  upstreams.emplace("fake_clusterA", upstream_a);
  upstreams.emplace("fake_clusterB", std::make_shared<ConnPool::MockInstance>());

  auto prefix_routes = createPrefixRoutes();

  {
    auto* route = prefix_routes.mutable_routes()->Add();
    route->set_prefix("abc");
    route->set_cluster("fake_clusterA");
    route->set_remove_prefix(true);
  }

  EXPECT_CALL(*upstream_a, makeRequest(Eq(":bar"), _, _));

  PrefixRoutes router(prefix_routes, std::move(upstreams));
  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolCallbacks callbacks;

  EXPECT_EQ(nullptr, router.makeRequest("abc:bar", value, callbacks));
}

TEST(PrefixRoutesTest, RoutedToShortestPrefix) {
  auto upstream_b = std::make_shared<ConnPool::MockInstance>();

  Upstreams upstreams;
  upstreams.emplace("fake_clusterA", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("fake_clusterB", upstream_b);

  EXPECT_CALL(*upstream_b, makeRequest(Eq("a:bar"), _, _));

  PrefixRoutes router(createPrefixRoutes(), std::move(upstreams));
  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolCallbacks callbacks;

  EXPECT_EQ(nullptr, router.makeRequest("a:bar", value, callbacks));
}

TEST(PrefixRoutesTest, DifferentPrefixesSameUpstream) {
  auto upstream_b = std::make_shared<ConnPool::MockInstance>();

  Upstreams upstreams;
  upstreams.emplace("fake_clusterA", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("fake_clusterB", upstream_b);

  auto prefix_routes = createPrefixRoutes();

  {
    auto* route = prefix_routes.mutable_routes()->Add();
    route->set_prefix("also_route_to_b");
    route->set_cluster("fake_clusterB");
  }

  EXPECT_CALL(*upstream_b, makeRequest(Eq("a:bar"), _, _));
  EXPECT_CALL(*upstream_b, makeRequest(Eq("also_route_to_b:bar"), _, _));

  PrefixRoutes router(prefix_routes, std::move(upstreams));
  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolCallbacks callbacks;

  EXPECT_EQ(nullptr, router.makeRequest("a:bar", value, callbacks));
  EXPECT_EQ(nullptr, router.makeRequest("also_route_to_b:bar", value, callbacks));
}

TEST(PrefixRoutesTest, DuplicatePrefix) {
  Upstreams upstreams;
  upstreams.emplace("fake_clusterA", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("fake_clusterB", std::make_shared<ConnPool::MockInstance>());
  upstreams.emplace("this_will_throw", std::make_shared<ConnPool::MockInstance>());

  auto prefix_routes = createPrefixRoutes();

  {
    auto* route = prefix_routes.mutable_routes()->Add();
    route->set_prefix("ab");
    route->set_cluster("this_will_throw");
  }

  EXPECT_THROW_WITH_MESSAGE(PrefixRoutes router(prefix_routes, std::move(upstreams)),
                            EnvoyException, "prefix `ab` already exists.")
}

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
