#include <memory>
#include <string>

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/extensions/filters/network/redis_proxy/conn_pool_impl.h"
#include "source/extensions/filters/network/redis_proxy/hash_slot_router_impl.h"

#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/extensions/filters/network/redis_proxy/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/utility.h"

using testing::Eq;
using testing::Matcher;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

class HashSlotRoutesTest : public testing::Test {
public:
  void SetUp() override {
    hash_slot_routes.mutable_routes();
    upstreams.emplace("fake_clusterA", cluster_a);
    upstreams.emplace("fake_clusterB", cluster_b);
    upstreams.emplace("fake_clusterC", cluster_c);
  }

  Upstreams upstreams;
  std::shared_ptr<ConnPool::MockInstance> cluster_a = std::make_shared<ConnPool::MockInstance>();
  std::shared_ptr<ConnPool::MockInstance> cluster_b = std::make_shared<ConnPool::MockInstance>();
  std::shared_ptr<ConnPool::MockInstance> cluster_c = std::make_shared<ConnPool::MockInstance>();
  Runtime::MockLoader runtime_;
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::HashSlotRoutes hash_slot_routes;
};

TEST_F(HashSlotRoutesTest, RouteToExpectedClusters) {
  auto routes = hash_slot_routes.mutable_routes();
  {
    auto* route = routes->Add();
    auto* slots = route->mutable_slot_ranges();
    auto* slot = slots->Add();
    slot->set_start(0);
    slot->set_end(2000);
    route->set_cluster("fake_clusterA");
  }

  {
    auto* route = routes->Add();
    auto* slots = route->mutable_slot_ranges();
    auto* slot = slots->Add();
    slot->set_start(2001);
    slot->set_end(4000);
    route->set_cluster("fake_clusterB");
  }

  {
    auto* route = routes->Add();
    auto* slots = route->mutable_slot_ranges();
    auto* slot = slots->Add();
    slot->set_start(4001);
    slot->set_end(8191);
    route->set_cluster("fake_clusterC");
  }

  HashSlotRoutes router(hash_slot_routes, std::move(upstreams), runtime_);

  // Slot: xxHash64() % 8192 = 7129, going to cluster_c
  std::string testkey_1("testkey_a");
  // Slot: 393, going to cluster_a
  std::string testkey_2("testkey_ab");
  // Slot: 3249, going to cluster_b
  std::string testkey_3("testkey_abcd");

  EXPECT_EQ(cluster_c, router.upstreamPool(testkey_1)->upstream());
  EXPECT_EQ(cluster_a, router.upstreamPool(testkey_2)->upstream());
  EXPECT_EQ(cluster_b, router.upstreamPool(testkey_3)->upstream());
}

TEST_F(HashSlotRoutesTest, SetMultipleSlotRanges) {
  auto routes = hash_slot_routes.mutable_routes();
  {
    auto* route = routes->Add();
    auto* slots = route->mutable_slot_ranges();

    auto* slot1 = slots->Add();
    slot1->set_start(0);
    slot1->set_end(2000);

    auto* slot2 = slots->Add();
    slot2->set_start(4001);
    slot2->set_end(8191);

    route->set_cluster("fake_clusterA");
  }

  {
    auto* route = routes->Add();
    auto* slots = route->mutable_slot_ranges();
    auto* slot = slots->Add();
    slot->set_start(2001);
    slot->set_end(4000);
    route->set_cluster("fake_clusterB");
  }

  HashSlotRoutes router(hash_slot_routes, std::move(upstreams), runtime_);

  // Slot: xxHash64() % 8192 = 7129, going to cluster_a
  std::string testkey_1("testkey_a");
  // Slot: 393, going to cluster_a
  std::string testkey_2("testkey_ab");
  // Slot: 3249, going to cluster_b
  std::string testkey_3("testkey_abcd");

  EXPECT_EQ(cluster_a, router.upstreamPool(testkey_1)->upstream());
  EXPECT_EQ(cluster_a, router.upstreamPool(testkey_2)->upstream());
  EXPECT_EQ(cluster_b, router.upstreamPool(testkey_3)->upstream());
}

TEST_F(HashSlotRoutesTest, RouteToCatchAll) {
  auto routes = hash_slot_routes.mutable_routes();
  {
    auto* route = routes->Add();
    auto* slots = route->mutable_slot_ranges();
    auto* slot = slots->Add();
    slot->set_start(0);
    slot->set_end(2000);
    route->set_cluster("fake_clusterA");
  }

  hash_slot_routes.mutable_catch_all_route()->set_cluster("fake_clusterB");

  HashSlotRoutes router(hash_slot_routes, std::move(upstreams), runtime_);

  // Slot: xxHash64() % 8192 = 7129, going to cluster_b
  std::string testkey_1("testkey_a");
  // Slot: 393, going to cluster_a
  std::string testkey_2("testkey_ab");
  // Slot: 3249, going to cluster_b
  std::string testkey_3("testkey_abcd");

  EXPECT_EQ(cluster_b, router.upstreamPool(testkey_1)->upstream());
  EXPECT_EQ(cluster_a, router.upstreamPool(testkey_2)->upstream());
  EXPECT_EQ(cluster_b, router.upstreamPool(testkey_3)->upstream());
}

TEST_F(HashSlotRoutesTest, SlotConfigMissing) {
  auto routes = hash_slot_routes.mutable_routes();
  {
    auto* route = routes->Add();
    auto* slots = route->mutable_slot_ranges();
    auto* slot = slots->Add();
    slot->set_start(0);
    slot->set_end(2000);
    route->set_cluster("fake_clusterA");
  }

  EXPECT_THROW_WITH_MESSAGE(HashSlotRoutes router(hash_slot_routes, std::move(upstreams), runtime_),
                            EnvoyException,
                            "slot `2001` is not assigned and catch_all_route is not configured")
}

TEST_F(HashSlotRoutesTest, DuplicatedSlotConfig) {
  auto routes = hash_slot_routes.mutable_routes();
  {
    auto* route = routes->Add();
    auto* slots = route->mutable_slot_ranges();
    auto* slot = slots->Add();
    slot->set_start(0);
    slot->set_end(2000);
    route->set_cluster("fake_clusterA");
  }
  {
    auto* route = routes->Add();
    auto* slots = route->mutable_slot_ranges();
    auto* slot = slots->Add();
    slot->set_start(1000);
    slot->set_end(8191);
    route->set_cluster("fake_clusterB");
  }

  EXPECT_THROW_WITH_MESSAGE(HashSlotRoutes router(hash_slot_routes, std::move(upstreams), runtime_),
                            EnvoyException, "slot `1000` is already assigned.");
}

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
