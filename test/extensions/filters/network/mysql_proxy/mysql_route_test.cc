#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"

#include "extensions/filters/network/mysql_proxy/route_impl.h"

#include "gtest/gtest.h"
#include "mock.h"

using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {
envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy createConfig() {
  envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy config;
  auto* routes = config.mutable_routes();

  {
    auto* route = routes->Add();
    route->set_database("a");
    route->set_cluster("fake_clusterA");
  }
  {
    auto* route = routes->Add();
    route->set_database("b");
    route->set_cluster("fake_clusterB");
  }

  return config;
}

TEST(PrefixRoutesTest, BasicMatch) {
  auto config = createConfig();
  absl::flat_hash_map<std::string, RouteSharedPtr> routes;
  std::vector<ConnectionPool::ClientPoolSharedPtr> pools;
  for (const auto& route : config.routes()) {
    pools.emplace_back(std::make_shared<ConnectionPool::MockPool>(route.cluster()));
    auto route_ = std::make_shared<MockRoute>(*pools.back());
    EXPECT_CALL(*route_, upstream()).WillOnce(ReturnRef(*pools.back()));
    routes.emplace(route.database(), route_);
  }

  RouterImpl router(std::move(routes));
  EXPECT_EQ(nullptr, router.upstreamPool("c"));
  EXPECT_EQ("fake_clusterB",
            dynamic_cast<ConnectionPool::MockPool&>(router.upstreamPool("b")->upstream()).name);
  EXPECT_EQ("fake_clusterA",
            dynamic_cast<ConnectionPool::MockPool&>(router.upstreamPool("a")->upstream()).name);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy