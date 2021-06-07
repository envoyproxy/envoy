#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"

#include "source/extensions/filters/network/mysql_proxy/route_impl.h"

#include "test/mocks/upstream/mocks.h"

#include "gtest/gtest.h"
#include "mock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {
envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy createConfig() {
  envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy config;
  auto* database_routes = config.mutable_database_routes();
  auto* routes = database_routes->mutable_routes();

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
  auto* route = database_routes->mutable_catch_all_route();
  route->set_database("");
  route->set_cluster("fake_clusterC");

  return config;
}

TEST(PrefixRoutesTest, BasicMatch) {
  auto config = createConfig();
  absl::flat_hash_map<std::string, RouteSharedPtr> routes;
  std::vector<std::string> clusters;
  Upstream::MockClusterManager cm;
  for (const auto& route : config.database_routes().routes()) {
    clusters.emplace_back(route.cluster());
    auto route_ = std::make_shared<RouteImpl>(&cm, route.cluster());
    routes.emplace(route.database(), route_);
  }
  EXPECT_EQ("fake_clusterC", config.database_routes().catch_all_route().cluster());
  RouteSharedPtr default_cluster_route =
      std::make_shared<RouteImpl>(&cm, config.database_routes().catch_all_route().cluster());
  clusters.emplace_back(config.database_routes().catch_all_route().cluster());

  cm.initializeThreadLocalClusters(clusters);
  EXPECT_CALL(cm, getThreadLocalCluster).Times(3);
  RouterImpl router(default_cluster_route, std::move(routes));
  EXPECT_EQ(nullptr, router.upstreamPool("c"));
  EXPECT_NE(nullptr, router.upstreamPool("b")->upstream());
  EXPECT_NE(nullptr, router.upstreamPool("a")->upstream());
  EXPECT_NE(nullptr, router.defaultPool()->upstream());

  EXPECT_EQ("fake_clusterB", router.upstreamPool("b")->name());
  EXPECT_EQ("fake_clusterA", router.upstreamPool("a")->name());
  EXPECT_EQ("fake_clusterC", router.defaultPool()->name());
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
