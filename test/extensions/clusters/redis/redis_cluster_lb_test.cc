#include <memory>

#include "source/extensions/clusters/redis/redis_cluster_lb.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/upstream/mocks.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

class TestLoadBalancerContext : public Upstream::LoadBalancerContextBase {
public:
  TestLoadBalancerContext(uint64_t hash_key) : hash_key_(hash_key) {}

  TestLoadBalancerContext(absl::optional<uint64_t> hash) : hash_key_(hash) {}

  // Upstream::LoadBalancerContext
  absl::optional<uint64_t> computeHashKey() override { return hash_key_; }

  absl::optional<uint64_t> hash_key_;
};

class RedisClusterLoadBalancerTest : public testing::Test {
public:
  RedisClusterLoadBalancerTest() = default;

  void init() {
    factory_ = std::make_shared<RedisClusterLoadBalancerFactory>();
    lb_ = std::make_unique<RedisClusterThreadAwareLoadBalancer>(factory_);
    lb_->initialize();
  }

  void validateAssignment(Upstream::HostVector& hosts,
                          const std::vector<std::pair<uint32_t, uint32_t>> expected_assignments) {

    Upstream::LoadBalancerPtr lb = lb_->factory()->create();
    for (auto& assignment : expected_assignments) {
      TestLoadBalancerContext context(assignment.first);
      EXPECT_EQ(hosts[assignment.second]->address()->asString(),
                lb->chooseHost(&context)->address()->asString());
    }
  }

  std::shared_ptr<RedisClusterLoadBalancerFactory> factory_;
  SlotArraySharedPtr slot_array_;
  std::unique_ptr<RedisClusterThreadAwareLoadBalancer> lb_;
  std::shared_ptr<Upstream::MockClusterInfo> info_{new NiceMock<Upstream::MockClusterInfo>()};
};

// Works correctly without any hosts.
TEST_F(RedisClusterLoadBalancerTest, NoHost) {
  init();
  EXPECT_EQ(nullptr, lb_->factory()->create()->chooseHost(nullptr));
};

// Works correctly with empty context
TEST_F(RedisClusterLoadBalancerTest, NoHash) {
  Upstream::HostVector hosts{Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"),
                             Upstream::makeTestHost(info_, "tcp://127.0.0.1:91"),
                             Upstream::makeTestHost(info_, "tcp://127.0.0.1:92")};

  const std::vector<ClusterSlot> slots{
      ClusterSlot(0, 1000, hosts[0]->address()),
      ClusterSlot(1001, 2000, hosts[1]->address()),
      ClusterSlot(2001, 16383, hosts[2]->address()),
  };
  Upstream::HostMap all_hosts{
      {hosts[0]->address()->asString(), hosts[0]},
      {hosts[1]->address()->asString(), hosts[1]},
      {hosts[2]->address()->asString(), hosts[2]},
  };
  init();
  factory_->onClusterSlotUpdate(slots, all_hosts);
  TestLoadBalancerContext context(absl::nullopt);
  EXPECT_EQ(nullptr, lb_->factory()->create()->chooseHost(&context));
};

TEST_F(RedisClusterLoadBalancerTest, Basic) {
  Upstream::HostVector hosts{Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"),
                             Upstream::makeTestHost(info_, "tcp://127.0.0.1:91"),
                             Upstream::makeTestHost(info_, "tcp://127.0.0.1:92")};

  const std::vector<ClusterSlot> slots{
      ClusterSlot(0, 1000, hosts[0]->address()),
      ClusterSlot(1001, 2000, hosts[1]->address()),
      ClusterSlot(2001, 16383, hosts[2]->address()),
  };
  Upstream::HostMap all_hosts{
      {hosts[0]->address()->asString(), hosts[0]},
      {hosts[1]->address()->asString(), hosts[1]},
      {hosts[2]->address()->asString(), hosts[2]},
  };
  init();
  factory_->onClusterSlotUpdate(slots, all_hosts);

  // A list of (hash: host_index) pair
  const std::vector<std::pair<uint32_t, uint32_t>> expected_assignments = {
      {0, 0},    {100, 0},   {1000, 0}, {17382, 0}, {1001, 1},  {1100, 1},
      {2000, 1}, {18382, 1}, {2001, 2}, {2100, 2},  {16383, 2}, {19382, 2}};
  validateAssignment(hosts, expected_assignments);
}

TEST_F(RedisClusterLoadBalancerTest, ClusterSlotUpdate) {
  Upstream::HostVector hosts{Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"),
                             Upstream::makeTestHost(info_, "tcp://127.0.0.1:91")};
  const std::vector<ClusterSlot> slots{ClusterSlot(0, 1000, hosts[0]->address()),
                                       ClusterSlot(1001, 16383, hosts[1]->address())};
  Upstream::HostMap all_hosts{{hosts[0]->address()->asString(), hosts[0]},
                              {hosts[1]->address()->asString(), hosts[1]}};
  init();
  EXPECT_EQ(true, factory_->onClusterSlotUpdate(slots, all_hosts));

  // A list of initial (hash: host_index) pair
  const std::vector<std::pair<uint32_t, uint32_t>> original_assignments = {
      {100, 0}, {1100, 1}, {2100, 1}};

  validateAssignment(hosts, original_assignments);

  // Update the slot allocation should also change the assignment.
  std::vector<ClusterSlot> updated_slot{
      ClusterSlot(0, 1000, hosts[0]->address()),
      ClusterSlot(1001, 2000, hosts[1]->address()),
      ClusterSlot(2001, 16383, hosts[0]->address()),
  };
  EXPECT_EQ(true, factory_->onClusterSlotUpdate(updated_slot, all_hosts));

  // A list of updated (hash: host_index) pair.
  const std::vector<std::pair<uint32_t, uint32_t>> updated_assignments = {
      {100, 0}, {1100, 1}, {2100, 0}};
  validateAssignment(hosts, updated_assignments);
}

TEST_F(RedisClusterLoadBalancerTest, ClusterSlotNoUpdate) {
  Upstream::HostVector hosts{Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"),
                             Upstream::makeTestHost(info_, "tcp://127.0.0.1:91"),
                             Upstream::makeTestHost(info_, "tcp://127.0.0.1:92")};

  const std::vector<ClusterSlot> slots{
      ClusterSlot(0, 1000, hosts[0]->address()),
      ClusterSlot(1001, 2000, hosts[1]->address()),
      ClusterSlot(2001, 16383, hosts[2]->address()),
  };
  Upstream::HostMap all_hosts{
      {hosts[0]->address()->asString(), hosts[0]},
      {hosts[1]->address()->asString(), hosts[1]},
      {hosts[2]->address()->asString(), hosts[2]},
  };

  // A list of (hash: host_index) pair.
  const std::vector<std::pair<uint32_t, uint32_t>> expected_assignments = {
      {100, 0}, {1100, 1}, {2100, 2}};

  init();
  EXPECT_EQ(true, factory_->onClusterSlotUpdate(slots, all_hosts));
  validateAssignment(hosts, expected_assignments);

  // Calling cluster slot update without change should not change assignment.
  std::vector<ClusterSlot> updated_slot{
      ClusterSlot(0, 1000, hosts[0]->address()),
      ClusterSlot(1001, 2000, hosts[1]->address()),
      ClusterSlot(2001, 16383, hosts[2]->address()),
  };
  EXPECT_EQ(false, factory_->onClusterSlotUpdate(updated_slot, all_hosts));
  validateAssignment(hosts, expected_assignments);
}

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
