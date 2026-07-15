#include <memory>

#include "source/extensions/clusters/redis/redis_cluster_lb.h"
#include "source/extensions/filters/network/common/redis/client.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

class TestLoadBalancerContext : public RedisLoadBalancerContext,
                                public Upstream::LoadBalancerContextBase {
public:
  TestLoadBalancerContext(uint64_t hash_key, bool is_read,
                          NetworkFilters::Common::Redis::Client::ReadPolicy read_policy,
                          const std::string& client_zone = "")
      : hash_key_(hash_key), is_read_(is_read), read_policy_(read_policy),
        client_zone_(client_zone) {}

  TestLoadBalancerContext(std::optional<uint64_t> hash) : hash_key_(hash) {}

  // Upstream::LoadBalancerContext
  std::optional<uint64_t> computeHashKey() override { return hash_key_; }

  bool isReadCommand() const override { return is_read_; };
  NetworkFilters::Common::Redis::Client::ReadPolicy readPolicy() const override {
    return read_policy_;
  };
  const std::string& clientZone() const override { return client_zone_; }

  std::optional<uint64_t> hash_key_;
  bool is_read_{};
  NetworkFilters::Common::Redis::Client::ReadPolicy read_policy_{};
  std::string client_zone_;
};

class RedisClusterLoadBalancerTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  RedisClusterLoadBalancerTest() = default;

  // Helper to create a host with locality zone set
  Upstream::HostSharedPtr makeHostWithZone(const std::string& url, const std::string& zone) {
    envoy::config::core::v3::Locality locality;
    locality.set_zone(zone);
    return Upstream::makeTestHost(info_, url, locality);
  }

  void init() {
    factory_ = std::make_shared<RedisClusterLoadBalancerFactory>(random_);
    lb_ = std::make_unique<RedisClusterThreadAwareLoadBalancer>(factory_);
    EXPECT_OK(lb_->initialize());
    factory_->onHostHealthUpdate();
  }

  void validateAssignment(Upstream::HostVector& hosts,
                          const std::vector<std::pair<uint32_t, uint32_t>>& expected_assignments,
                          bool read_command = false,
                          NetworkFilters::Common::Redis::Client::ReadPolicy read_policy =
                              NetworkFilters::Common::Redis::Client::ReadPolicy::Primary,
                          const std::string& client_zone = "") {

    Upstream::LoadBalancerPtr lb = lb_->factory()->create(lb_params_);
    for (auto& assignment : expected_assignments) {
      TestLoadBalancerContext context(assignment.first, read_command, read_policy, client_zone);
      auto host = lb->chooseHost(&context).host;
      EXPECT_FALSE(host == nullptr);
      EXPECT_EQ(hosts[assignment.second]->address()->asString(), host->address()->asString());
    }
  }

  static std::pair<std::string, Upstream::HostSharedPtr>
  makePair(const Upstream::HostSharedPtr& host) {
    return {host->address()->asString(), host};
  }

  Upstream::HostMap generateHostMap(Upstream::HostVector& hosts) {
    Upstream::HostMap map;
    std::transform(hosts.begin(), hosts.end(), std::inserter(map, map.end()), makePair);
    return map;
  }

  std::shared_ptr<RedisClusterLoadBalancerFactory> factory_;
  std::unique_ptr<RedisClusterThreadAwareLoadBalancer> lb_;
  std::shared_ptr<Upstream::MockClusterInfo> info_{new NiceMock<Upstream::MockClusterInfo>()};
  NiceMock<Random::MockRandomGenerator> random_;

  // Just use this as parameters of create() method but thread aware load balancer will not use it.
  NiceMock<Upstream::MockPrioritySet> worker_priority_set_;
  Upstream::LoadBalancerParams lb_params_{worker_priority_set_, {}};
};

class RedisLoadBalancerContextImplTest : public testing::Test {
public:
  void makeBulkStringArray(NetworkFilters::Common::Redis::RespValue& value,
                           const std::vector<std::string>& strings) {
    std::vector<NetworkFilters::Common::Redis::RespValue> values(strings.size());
    for (uint64_t i = 0; i < strings.size(); i++) {
      values[i].type(NetworkFilters::Common::Redis::RespType::BulkString);
      values[i].asString() = strings[i];
    }

    value.type(NetworkFilters::Common::Redis::RespType::Array);
    value.asArray().swap(values);
  }
};

// Works correctly without any hosts.
TEST_F(RedisClusterLoadBalancerTest, NoHost) {
  init();
  EXPECT_EQ(nullptr, lb_->factory()->create(lb_params_)->chooseHost(nullptr).host);
};

// Verify stub LB interface methods return expected defaults.
TEST_F(RedisClusterLoadBalancerTest, LoadBalancerStubMethods) {
  Upstream::HostVector hosts{Upstream::makeTestHost(info_, "tcp://127.0.0.1:90")};
  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(
      std::vector<ClusterSlot>{ClusterSlot(0, 16383, hosts[0]->address())});
  Upstream::HostMap all_hosts = generateHostMap(hosts);
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  Upstream::LoadBalancerPtr lb = lb_->factory()->create(lb_params_);

  // peekAnotherHost is not implemented and always returns nullptr.
  EXPECT_EQ(nullptr, lb->peekAnotherHost(nullptr));

  // selectExistingConnection is not implemented and always returns nullopt.
  std::vector<uint8_t> hash_key;
  EXPECT_EQ(std::nullopt, lb->selectExistingConnection(nullptr, *hosts[0], hash_key));

  // lifetimeCallbacks is not implemented and returns empty OptRef.
  EXPECT_FALSE(lb->lifetimeCallbacks().has_value());
}

// Works correctly with empty context
TEST_F(RedisClusterLoadBalancerTest, NoHash) {
  Upstream::HostVector hosts{Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"),
                             Upstream::makeTestHost(info_, "tcp://127.0.0.1:91"),
                             Upstream::makeTestHost(info_, "tcp://127.0.0.1:92")};

  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 1000, hosts[0]->address()),
      ClusterSlot(1001, 2000, hosts[1]->address()),
      ClusterSlot(2001, 16383, hosts[2]->address()),
  });
  Upstream::HostMap all_hosts{
      {hosts[0]->address()->asString(), hosts[0]},
      {hosts[1]->address()->asString(), hosts[1]},
      {hosts[2]->address()->asString(), hosts[2]},
  };
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);
  TestLoadBalancerContext context(std::nullopt);
  EXPECT_EQ(nullptr, lb_->factory()->create(lb_params_)->chooseHost(&context).host);
};

TEST_F(RedisClusterLoadBalancerTest, Basic) {
  Upstream::HostVector hosts{Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"),
                             Upstream::makeTestHost(info_, "tcp://127.0.0.1:91"),
                             Upstream::makeTestHost(info_, "tcp://127.0.0.1:92")};

  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 1000, hosts[0]->address()),
      ClusterSlot(1001, 2000, hosts[1]->address()),
      ClusterSlot(2001, 16383, hosts[2]->address()),
  });
  Upstream::HostMap all_hosts{
      {hosts[0]->address()->asString(), hosts[0]},
      {hosts[1]->address()->asString(), hosts[1]},
      {hosts[2]->address()->asString(), hosts[2]},
  };
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  // A list of (hash: host_index) pair
  const std::vector<std::pair<uint32_t, uint32_t>> expected_assignments = {
      {0, 0},    {100, 0},   {1000, 0}, {17382, 0}, {1001, 1},  {1100, 1},
      {2000, 1}, {18382, 1}, {2001, 2}, {2100, 2},  {16383, 2}, {19382, 2}};
  validateAssignment(hosts, expected_assignments);
}

TEST_F(RedisClusterLoadBalancerTest, Shard) {
  Upstream::HostVector hosts{Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"),
                             Upstream::makeTestHost(info_, "tcp://127.0.0.1:91"),
                             Upstream::makeTestHost(info_, "tcp://127.0.0.1:92")};

  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 1000, hosts[0]->address()),
      ClusterSlot(1001, 2000, hosts[1]->address()),
      ClusterSlot(2001, 16383, hosts[2]->address()),
  });
  Upstream::HostMap all_hosts{
      {hosts[0]->address()->asString(), hosts[0]},
      {hosts[1]->address()->asString(), hosts[1]},
      {hosts[2]->address()->asString(), hosts[2]},
  };
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  // A list of (hash: host_index) pair
  // Simple read command
  std::vector<NetworkFilters::Common::Redis::RespValue> get_foo(2);
  get_foo[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  get_foo[0].asString() = "get";
  get_foo[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
  get_foo[1].asString() = "foo";

  NetworkFilters::Common::Redis::RespValue get_request;
  get_request.type(NetworkFilters::Common::Redis::RespType::Array);
  get_request.asArray().swap(get_foo);

  Upstream::LoadBalancerPtr lb = lb_->factory()->create(lb_params_);
  for (uint16_t i = 0; i < 5; i++) {
    RedisSpecifyShardContextImpl context(i, get_request);
    auto host = lb->chooseHost(&context).host;
    if (i < 3) {
      EXPECT_FALSE(host == nullptr);
      EXPECT_EQ(hosts[i]->address()->asString(), host->address()->asString());
    } else {
      EXPECT_TRUE(host == nullptr);
    }
  }
}

// --- shard membership exposure (ShardMembershipResolver / redisSlotForKey) ---

// membersForSlot returns the shard owning an assigned slot: primary first, then replicas, in the
// snapshot's order. This is what replica-capable read-policy placement fans a channel across.
TEST_F(RedisClusterLoadBalancerTest, MembersForSlotReturnsShardOfAssignedSlot) {
  Upstream::HostVector hosts{
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"), // shard 0 primary
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:91"), // shard 1 primary
      Upstream::makeTestHost(info_, "tcp://127.0.0.2:90"), // shard 0 replica
      Upstream::makeTestHost(info_, "tcp://127.0.0.2:91"), // shard 1 replica
  };
  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 2000, hosts[0]->address()),
      ClusterSlot(2001, 16383, hosts[1]->address()),
  });
  slots->at(0).addReplica(hosts[2]->address());
  slots->at(1).addReplica(hosts[3]->address());
  Upstream::HostMap all_hosts;
  std::transform(hosts.begin(), hosts.end(), std::inserter(all_hosts, all_hosts.end()), makePair);
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  Upstream::LoadBalancerPtr lb = lb_->factory()->create(lb_params_);
  auto* resolver = dynamic_cast<ShardMembershipResolver*>(lb.get());
  ASSERT_NE(nullptr, resolver);

  // Slot in shard 0's range: primary hosts[0], members {hosts[0], hosts[2]}.
  auto s0 = resolver->membersForSlot(1000);
  ASSERT_TRUE(s0.has_value());
  EXPECT_EQ(hosts[0]->address()->asString(), s0->all_hosts->front()->address()->asString());
  ASSERT_EQ(2, s0->all_hosts->size());
  EXPECT_EQ(hosts[0]->address()->asString(),
            (*s0->all_hosts)[0]->address()->asString()); // primary 1st
  EXPECT_EQ(hosts[2]->address()->asString(), (*s0->all_hosts)[1]->address()->asString());

  // Slot in shard 1's range: primary hosts[1], members {hosts[1], hosts[3]}.
  auto s1 = resolver->membersForSlot(10000);
  ASSERT_TRUE(s1.has_value());
  EXPECT_EQ(hosts[1]->address()->asString(), s1->all_hosts->front()->address()->asString());
  ASSERT_EQ(2, s1->all_hosts->size());
  EXPECT_EQ(hosts[1]->address()->asString(), (*s1->all_hosts)[0]->address()->asString());
  EXPECT_EQ(hosts[3]->address()->asString(), (*s1->all_hosts)[1]->address()->asString());
}

// membersForSlot yields nullopt before any topology snapshot exists and for an out-of-range slot.
TEST_F(RedisClusterLoadBalancerTest, MembersForSlotNullWithoutSnapshotOrOutOfRange) {
  init();
  // No onClusterSlotUpdate yet -> the freshly created LB carries no snapshot.
  Upstream::LoadBalancerPtr lb_no_snapshot = lb_->factory()->create(lb_params_);
  auto* resolver_empty = dynamic_cast<ShardMembershipResolver*>(lb_no_snapshot.get());
  ASSERT_NE(nullptr, resolver_empty);
  EXPECT_FALSE(resolver_empty->membersForSlot(0).has_value());

  // Assign one shard over the whole keyspace, then probe in-range vs out-of-range slots.
  Upstream::HostVector hosts{Upstream::makeTestHost(info_, "tcp://127.0.0.1:90")};
  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(
      std::vector<ClusterSlot>{ClusterSlot(0, 16383, hosts[0]->address())});
  Upstream::HostMap all_hosts{{hosts[0]->address()->asString(), hosts[0]}};
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  Upstream::LoadBalancerPtr lb = lb_->factory()->create(lb_params_);
  auto* resolver = dynamic_cast<ShardMembershipResolver*>(lb.get());
  ASSERT_NE(nullptr, resolver);
  EXPECT_TRUE(resolver->membersForSlot(0).has_value());        // in range
  EXPECT_FALSE(resolver->membersForSlot(MaxSlot).has_value()); // == 16384, out of range
  EXPECT_FALSE(resolver->membersForSlot(MaxSlot + 5).has_value());
}

// membersForSlot honors its nullopt contract for an in-range slot that CLUSTER SLOTS did not cover
// (a partial-provisioning/migration window). Without the unassigned sentinel the slot would map to
// the zero-initialized index 0 and wrongly return shard 0's members.

// Data-path counterpart of the unassigned sentinel: chooseHost on an in-range but uncovered slot
// falls back to shard 0 (whose -MOVED reply drives the redirect) instead of indexing past the
// shard vector.
// ClusterSlot equality: a primary-address mismatch short-circuits false (the dedup gate in
// onClusterSlotUpdate).
// LOCAL_ZONE_AFFINITY with an empty client zone: the per-zone replica lookup returns the shared
// null set and routing falls back (any replica, then primary) instead of crashing or picking a
// wrong host.
TEST_F(RedisClusterLoadBalancerTest, LocalZoneAffinityEmptyClientZoneFallsBack) {
  Upstream::HostVector hosts{
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"),
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:91"),
  };
  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 16383, hosts[0]->address()),
  });
  (*slots)[0].addReplica(hosts[1]->address());
  Upstream::HostMap all_hosts{
      {hosts[0]->address()->asString(), hosts[0]},
      {hosts[1]->address()->asString(), hosts[1]},
  };
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  TestLoadBalancerContext context(
      0, /*is_read=*/true, NetworkFilters::Common::Redis::Client::ReadPolicy::LocalZoneAffinity,
      "");
  EXPECT_NE(nullptr, lb_->factory()->create(lb_params_)->chooseHost(&context).host);
}

TEST_F(RedisClusterLoadBalancerTest, ClusterSlotInequalityOnPrimaryMismatch) {
  Upstream::HostVector hosts{
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"),
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:91"),
  };
  ClusterSlot a(0, 5, hosts[0]->address());
  ClusterSlot b(0, 5, hosts[1]->address());
  EXPECT_FALSE(a == b);
  ClusterSlot c(0, 5, hosts[0]->address());
  c.addReplica(hosts[1]->address());
  EXPECT_FALSE(a == c);
}

TEST_F(RedisClusterLoadBalancerTest, ChooseHostFallsBackToShardZeroForUnassignedSlot) {
  Upstream::HostVector hosts{
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"),
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:91"),
  };
  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 1000, hosts[0]->address()),
      ClusterSlot(5000, 16383, hosts[1]->address()),
  });
  Upstream::HostMap all_hosts{
      {hosts[0]->address()->asString(), hosts[0]},
      {hosts[1]->address()->asString(), hosts[1]},
  };
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  // Slot 3000 is unassigned; hash 3000 maps to it directly (hash % 16384 == 3000).
  TestLoadBalancerContext context(3000, /*is_read=*/false,
                                  NetworkFilters::Common::Redis::Client::ReadPolicy::Primary);
  auto host = lb_->factory()->create(lb_params_)->chooseHost(&context).host;
  ASSERT_NE(nullptr, host);
  EXPECT_EQ(hosts[0]->address()->asString(), host->address()->asString());
}
TEST_F(RedisClusterLoadBalancerTest, MembersForSlotNullForUnassignedInRangeSlot) {
  Upstream::HostVector hosts{
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"), // shard 0 primary
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:91"), // shard 1 primary
  };
  // Deliberately leave slots 1001..4999 UNASSIGNED (a coverage gap).
  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 1000, hosts[0]->address()),
      ClusterSlot(5000, 16383, hosts[1]->address()),
  });
  Upstream::HostMap all_hosts;
  std::transform(hosts.begin(), hosts.end(), std::inserter(all_hosts, all_hosts.end()), makePair);
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  Upstream::LoadBalancerPtr lb = lb_->factory()->create(lb_params_);
  auto* resolver = dynamic_cast<ShardMembershipResolver*>(lb.get());
  ASSERT_NE(nullptr, resolver);

  EXPECT_TRUE(resolver->membersForSlot(500).has_value());   // covered by shard 0
  EXPECT_TRUE(resolver->membersForSlot(10000).has_value()); // covered by shard 1
  // In-range but uncovered slots -> nullopt, NOT silently shard 0.
  EXPECT_FALSE(resolver->membersForSlot(1001).has_value());
  EXPECT_FALSE(resolver->membersForSlot(2000).has_value());
  EXPECT_FALSE(resolver->membersForSlot(4999).has_value());
}

// A new LB reflects the latest factory snapshot: after a migration moves a slot's range to
// a different primary, membersForSlot on a fresh LB returns the new owner.
TEST_F(RedisClusterLoadBalancerTest, MembersForSlotReflectsUpdatedSnapshot) {
  Upstream::HostVector hosts{
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"),
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:91"),
  };
  Upstream::HostMap all_hosts;
  std::transform(hosts.begin(), hosts.end(), std::inserter(all_hosts, all_hosts.end()), makePair);
  init();

  // First snapshot: hosts[0] owns the whole keyspace.
  factory_->onClusterSlotUpdate(std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
                                    ClusterSlot(0, 16383, hosts[0]->address())}),
                                all_hosts);
  {
    Upstream::LoadBalancerPtr lb = lb_->factory()->create(lb_params_);
    auto* resolver = dynamic_cast<ShardMembershipResolver*>(lb.get());
    ASSERT_NE(nullptr, resolver);
    auto m = resolver->membersForSlot(1234);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(hosts[0]->address()->asString(), m->all_hosts->front()->address()->asString());
  }

  // Slot move: hosts[1] takes over slot 1234's range. A new LB must see the new owner.
  factory_->onClusterSlotUpdate(std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
                                    ClusterSlot(0, 1000, hosts[0]->address()),
                                    ClusterSlot(1001, 16383, hosts[1]->address())}),
                                all_hosts);
  {
    Upstream::LoadBalancerPtr lb = lb_->factory()->create(lb_params_);
    auto* resolver = dynamic_cast<ShardMembershipResolver*>(lb.get());
    ASSERT_NE(nullptr, resolver);
    auto m = resolver->membersForSlot(1234);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(hosts[1]->address()->asString(), m->all_hosts->front()->address()->asString());
  }
}

// redisSlotForKey maps a channel/key to the SAME slot the data path routes it to (so a channel's
// subscription placement and its SPUBLISH land on one shard), and applies Redis hash-tag isolation.
TEST_F(RedisClusterLoadBalancerTest, RedisSlotForKeyMatchesDataPathRouting) {
  Upstream::HostVector hosts{
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"),
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:91"),
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:92"),
  };
  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 1000, hosts[0]->address()),
      ClusterSlot(1001, 2000, hosts[1]->address()),
      ClusterSlot(2001, 16383, hosts[2]->address()),
  });
  Upstream::HostMap all_hosts;
  std::transform(hosts.begin(), hosts.end(), std::inserter(all_hosts, all_hosts.end()), makePair);
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  Upstream::LoadBalancerPtr lb = lb_->factory()->create(lb_params_);
  auto* resolver = dynamic_cast<ShardMembershipResolver*>(lb.get());
  ASSERT_NE(nullptr, resolver);

  // Hash-tag isolation: {foo} governs the slot regardless of surrounding text.
  EXPECT_EQ(redisSlotForKey("foo"), redisSlotForKey("{foo}bar"));
  EXPECT_EQ(redisSlotForKey("foo"), redisSlotForKey("prefix{foo}suffix"));

  // For several channel names, the slot resolver's primary agrees with the data path's Primary
  // routing for the same key hash.
  for (const std::string key : {"channel-a", "news.sports", "{tag}x", "42"}) {
    const uint64_t slot = redisSlotForKey(key);
    EXPECT_LT(slot, MaxSlot);
    const uint64_t hash = Crc16::crc16(RedisLoadBalancerContextImpl::hashtag(key, true));
    TestLoadBalancerContext ctx(hash, /*is_read=*/true,
                                NetworkFilters::Common::Redis::Client::ReadPolicy::Primary);
    auto routed = lb->chooseHost(&ctx).host;
    ASSERT_NE(nullptr, routed);
    auto members = resolver->membersForSlot(slot);
    ASSERT_TRUE(members.has_value());
    EXPECT_EQ(routed->address()->asString(), members->all_hosts->front()->address()->asString());
  }
}

TEST_F(RedisClusterLoadBalancerTest, ReadStrategiesHealthy) {
  Upstream::HostVector hosts{
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"),
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:91"),
      Upstream::makeTestHost(info_, "tcp://127.0.0.2:90"),
      Upstream::makeTestHost(info_, "tcp://127.0.0.2:91"),
  };

  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 2000, hosts[0]->address()),
      ClusterSlot(2001, 16383, hosts[1]->address()),
  });
  slots->at(0).addReplica(hosts[2]->address());
  slots->at(1).addReplica(hosts[3]->address());
  Upstream::HostMap all_hosts;
  std::transform(hosts.begin(), hosts.end(), std::inserter(all_hosts, all_hosts.end()), makePair);
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  // A list of (hash: host_index) pair
  const std::vector<std::pair<uint32_t, uint32_t>> replica_assignments = {
      {0, 2}, {1100, 2}, {2000, 2}, {18382, 2}, {2001, 3}, {2100, 3}, {16383, 3}, {19382, 3}};
  validateAssignment(hosts, replica_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Replica);
  validateAssignment(hosts, replica_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::PreferReplica);

  const std::vector<std::pair<uint32_t, uint32_t>> primary_assignments = {
      {0, 0}, {1100, 0}, {2000, 0}, {18382, 0}, {2001, 1}, {2100, 1}, {16383, 1}, {19382, 1}};
  validateAssignment(hosts, primary_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Primary);
  validateAssignment(hosts, primary_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::PreferPrimary);

  ON_CALL(random_, random()).WillByDefault(Return(0));
  validateAssignment(hosts, primary_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Any);
  ON_CALL(random_, random()).WillByDefault(Return(1));
  validateAssignment(hosts, replica_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Any);
}

TEST_F(RedisClusterLoadBalancerTest, ReadStrategiesUnhealthyPrimary) {
  Upstream::HostVector hosts{
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"),
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:91"),
      Upstream::makeTestHost(info_, "tcp://127.0.0.2:90"),
      Upstream::makeTestHost(info_, "tcp://127.0.0.2:91"),
  };

  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 2000, hosts[0]->address()),
      ClusterSlot(2001, 16383, hosts[1]->address()),
  });
  slots->at(0).addReplica(hosts[2]->address());
  slots->at(1).addReplica(hosts[3]->address());
  Upstream::HostMap all_hosts;
  std::transform(hosts.begin(), hosts.end(), std::inserter(all_hosts, all_hosts.end()), makePair);
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  hosts[0]->healthFlagSet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC);
  hosts[1]->healthFlagSet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC);

  factory_->onHostHealthUpdate();

  // A list of (hash: host_index) pair
  const std::vector<std::pair<uint32_t, uint32_t>> replica_assignments = {
      {0, 2}, {1100, 2}, {2000, 2}, {18382, 2}, {2001, 3}, {2100, 3}, {16383, 3}, {19382, 3}};
  const std::vector<std::pair<uint32_t, uint32_t>> primary_assignments = {
      {0, 0}, {1100, 0}, {2000, 0}, {18382, 0}, {2001, 1}, {2100, 1}, {16383, 1}, {19382, 1}};

  validateAssignment(hosts, replica_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Replica);
  validateAssignment(hosts, replica_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::PreferReplica);
  validateAssignment(hosts, primary_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Primary);
  validateAssignment(hosts, replica_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::PreferPrimary);

  ON_CALL(random_, random()).WillByDefault(Return(0));
  validateAssignment(hosts, replica_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Any);
  ON_CALL(random_, random()).WillByDefault(Return(1));
  validateAssignment(hosts, replica_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Any);
}

TEST_F(RedisClusterLoadBalancerTest, ReadStrategiesUnhealthyReplica) {
  Upstream::HostVector hosts{
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"),
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:91"),
      Upstream::makeTestHost(info_, "tcp://127.0.0.2:90"),
      Upstream::makeTestHost(info_, "tcp://127.0.0.2:91"),
  };

  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 2000, hosts[0]->address()),
      ClusterSlot(2001, 16383, hosts[1]->address()),
  });
  slots->at(0).addReplica(hosts[2]->address());
  slots->at(1).addReplica(hosts[3]->address());
  Upstream::HostMap all_hosts;
  std::transform(hosts.begin(), hosts.end(), std::inserter(all_hosts, all_hosts.end()), makePair);
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  hosts[2]->healthFlagSet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC);
  hosts[3]->healthFlagSet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC);

  factory_->onHostHealthUpdate();

  // A list of (hash: host_index) pair
  const std::vector<std::pair<uint32_t, uint32_t>> replica_assignments = {
      {0, 2}, {1100, 2}, {2000, 2}, {18382, 2}, {2001, 3}, {2100, 3}, {16383, 3}, {19382, 3}};
  const std::vector<std::pair<uint32_t, uint32_t>> primary_assignments = {
      {0, 0}, {1100, 0}, {2000, 0}, {18382, 0}, {2001, 1}, {2100, 1}, {16383, 1}, {19382, 1}};

  validateAssignment(hosts, replica_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Replica);
  validateAssignment(hosts, primary_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::PreferReplica);
  validateAssignment(hosts, primary_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Primary);
  validateAssignment(hosts, primary_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::PreferPrimary);

  ON_CALL(random_, random()).WillByDefault(Return(0));
  validateAssignment(hosts, primary_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Any);
  ON_CALL(random_, random()).WillByDefault(Return(1));
  validateAssignment(hosts, primary_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Any);
}

TEST_F(RedisClusterLoadBalancerTest, ReadStrategiesNoReplica) {
  Upstream::HostVector hosts{Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"),
                             Upstream::makeTestHost(info_, "tcp://127.0.0.1:91")};

  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 2000, hosts[0]->address()),
      ClusterSlot(2001, 16383, hosts[1]->address()),
  });
  Upstream::HostMap all_hosts;
  std::transform(hosts.begin(), hosts.end(), std::inserter(all_hosts, all_hosts.end()), makePair);
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  // A list of (hash: host_index) pair
  const std::vector<std::pair<uint32_t, uint32_t>> primary_assignments = {
      {0, 0}, {1100, 0}, {2000, 0}, {18382, 0}, {2001, 1}, {2100, 1}, {16383, 1}, {19382, 1}};
  validateAssignment(hosts, primary_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Primary);
  validateAssignment(hosts, primary_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::PreferPrimary);
  validateAssignment(hosts, primary_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Any);
  validateAssignment(hosts, primary_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::PreferReplica);

  Upstream::LoadBalancerPtr lb = lb_->factory()->create(lb_params_);
  TestLoadBalancerContext context(1100, true,
                                  NetworkFilters::Common::Redis::Client::ReadPolicy::Replica);
  auto host = lb->chooseHost(&context).host;
  EXPECT_TRUE(host == nullptr);
}

TEST_F(RedisClusterLoadBalancerTest, ClusterSlotUpdate) {
  Upstream::HostVector hosts{Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"),
                             Upstream::makeTestHost(info_, "tcp://127.0.0.1:91")};
  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 1000, hosts[0]->address()), ClusterSlot(1001, 16383, hosts[1]->address())});
  Upstream::HostMap all_hosts{{hosts[0]->address()->asString(), hosts[0]},
                              {hosts[1]->address()->asString(), hosts[1]}};
  init();
  EXPECT_EQ(true, factory_->onClusterSlotUpdate(std::move(slots), all_hosts));

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
  EXPECT_EQ(true, factory_->onClusterSlotUpdate(
                      std::make_unique<std::vector<ClusterSlot>>(updated_slot), all_hosts));

  // A list of updated (hash: host_index) pair.
  const std::vector<std::pair<uint32_t, uint32_t>> updated_assignments = {
      {100, 0}, {1100, 1}, {2100, 0}};
  validateAssignment(hosts, updated_assignments);
}

// Verifies that a worker-local LB instance refreshes its slot and shard
// snapshot when the worker priority set fires its member update callback,
// rather than relying on the cluster manager to recreate the LB.
TEST_F(RedisClusterLoadBalancerTest, LoadBalancerRefreshesOnMemberUpdate) {
  Upstream::HostVector hosts{Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"),
                             Upstream::makeTestHost(info_, "tcp://127.0.0.1:91")};
  Upstream::HostMap all_hosts{{hosts[0]->address()->asString(), hosts[0]},
                              {hosts[1]->address()->asString(), hosts[1]}};
  init();

  // Install the initial slot assignment.
  factory_->onClusterSlotUpdate(std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
                                    ClusterSlot(0, 1000, hosts[0]->address()),
                                    ClusterSlot(1001, 16383, hosts[1]->address())}),
                                all_hosts);

  // Create a single LB *before* any further slot updates and exercise it.
  Upstream::LoadBalancerPtr lb = lb_->factory()->create(lb_params_);
  {
    TestLoadBalancerContext context(100); // slot 100 → hosts[0]
    EXPECT_EQ(hosts[0]->address()->asString(),
              lb->chooseHost(&context).host->address()->asString());
    TestLoadBalancerContext context2(2100); // slot 2100 → hosts[1]
    EXPECT_EQ(hosts[1]->address()->asString(),
              lb->chooseHost(&context2).host->address()->asString());
  }

  // Update slot assignment in the factory so that slot 2100 now maps to hosts[0].
  factory_->onClusterSlotUpdate(
      std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
          ClusterSlot(0, 1000, hosts[0]->address()), ClusterSlot(1001, 2000, hosts[1]->address()),
          ClusterSlot(2001, 16383, hosts[0]->address())}),
      all_hosts);

  // Until the worker priority set fires its member update callback, the LB
  // still holds the previous snapshot.
  {
    TestLoadBalancerContext context(2100);
    EXPECT_EQ(hosts[1]->address()->asString(),
              lb->chooseHost(&context).host->address()->asString());
  }

  // Simulate the worker host update broadcast: the cluster manager calls
  // priority_set_.updateHosts on the worker, which fires every registered
  // MemberUpdateCb -- including the one the LB installed in its constructor.
  worker_priority_set_.runUpdateCallbacks(0, {}, {});

  // The same LB instance now picks the updated assignment.
  {
    TestLoadBalancerContext context(2100);
    EXPECT_EQ(hosts[0]->address()->asString(),
              lb->chooseHost(&context).host->address()->asString());
  }
}

TEST_F(RedisClusterLoadBalancerTest, ClusterSlotNoUpdate) {
  Upstream::HostVector hosts{Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"),
                             Upstream::makeTestHost(info_, "tcp://127.0.0.1:91"),
                             Upstream::makeTestHost(info_, "tcp://127.0.0.1:92"),
                             Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"),
                             Upstream::makeTestHost(info_, "tcp://127.0.0.1:91"),
                             Upstream::makeTestHost(info_, "tcp://127.0.0.1:92")};
  Upstream::HostVector replicas{Upstream::makeTestHost(info_, "tcp://127.0.0.2:90"),
                                Upstream::makeTestHost(info_, "tcp://127.0.0.2:91"),
                                Upstream::makeTestHost(info_, "tcp://127.0.0.2:90"),
                                Upstream::makeTestHost(info_, "tcp://127.0.0.2:91")};

  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 1000, hosts[0]->address()),
      ClusterSlot(1001, 2000, hosts[1]->address()),
      ClusterSlot(2001, 16383, hosts[2]->address()),
  });

  (*slots)[0].addReplica(replicas[0]->address());
  (*slots)[0].addReplica(replicas[1]->address());
  Upstream::HostMap all_hosts{
      {hosts[0]->address()->asString(), hosts[0]},
      {hosts[1]->address()->asString(), hosts[1]},
      {hosts[2]->address()->asString(), hosts[2]},
      {replicas[0]->address()->asString(), replicas[0]},
      {replicas[1]->address()->asString(), replicas[1]},
  };

  // A list of (hash: host_index) pair.
  const std::vector<std::pair<uint32_t, uint32_t>> expected_assignments = {
      {100, 0}, {1100, 1}, {2100, 2}};

  init();
  EXPECT_EQ(true, factory_->onClusterSlotUpdate(std::move(slots), all_hosts));
  validateAssignment(hosts, expected_assignments);

  // Calling cluster slot update without change should not change assignment.
  std::vector<ClusterSlot> updated_slot{
      ClusterSlot(0, 1000, hosts[3]->address()),
      ClusterSlot(1001, 2000, hosts[4]->address()),
      ClusterSlot(2001, 16383, hosts[5]->address()),
  };
  updated_slot[0].addReplica(replicas[3]->address());
  updated_slot[0].addReplica(replicas[2]->address());
  EXPECT_EQ(false, factory_->onClusterSlotUpdate(
                       std::make_unique<std::vector<ClusterSlot>>(updated_slot), all_hosts));
  validateAssignment(hosts, expected_assignments);
}

TEST_F(RedisLoadBalancerContextImplTest, Basic) {
  // Simple read command
  std::vector<NetworkFilters::Common::Redis::RespValue> get_foo(2);
  get_foo[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  get_foo[0].asString() = "get";
  get_foo[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
  get_foo[1].asString() = "foo";

  NetworkFilters::Common::Redis::RespValue get_request;
  get_request.type(NetworkFilters::Common::Redis::RespType::Array);
  get_request.asArray().swap(get_foo);

  RedisLoadBalancerContextImpl context1("foo", true, true, get_request,
                                        NetworkFilters::Common::Redis::Client::ReadPolicy::Primary);

  EXPECT_EQ(std::optional<uint64_t>(44950), context1.computeHashKey());
  EXPECT_EQ(true, context1.isReadCommand());
  EXPECT_EQ(NetworkFilters::Common::Redis::Client::ReadPolicy::Primary, context1.readPolicy());

  // Simple write command
  std::vector<NetworkFilters::Common::Redis::RespValue> set_foo(3);
  set_foo[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  set_foo[0].asString() = "set";
  set_foo[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
  set_foo[1].asString() = "foo";
  set_foo[2].type(NetworkFilters::Common::Redis::RespType::BulkString);
  set_foo[2].asString() = "bar";

  NetworkFilters::Common::Redis::RespValue set_request;
  set_request.type(NetworkFilters::Common::Redis::RespType::Array);
  set_request.asArray().swap(set_foo);

  RedisLoadBalancerContextImpl context2("foo", true, true, set_request,
                                        NetworkFilters::Common::Redis::Client::ReadPolicy::Primary);

  EXPECT_EQ(std::optional<uint64_t>(44950), context2.computeHashKey());
  EXPECT_EQ(false, context2.isReadCommand());
  EXPECT_EQ(NetworkFilters::Common::Redis::Client::ReadPolicy::Primary, context2.readPolicy());
}

TEST_F(RedisLoadBalancerContextImplTest, CompositeArray) {

  NetworkFilters::Common::Redis::RespValueSharedPtr base =
      std::make_shared<NetworkFilters::Common::Redis::RespValue>();
  makeBulkStringArray(*base, {"get", "foo", "bar"});

  // Composite read command
  NetworkFilters::Common::Redis::RespValue get_command;
  get_command.type(NetworkFilters::Common::Redis::RespType::SimpleString);
  get_command.asString() = "get";

  NetworkFilters::Common::Redis::RespValue get_request1{base, get_command, 1, 1};
  NetworkFilters::Common::Redis::RespValue get_request2{base, get_command, 2, 2};

  RedisLoadBalancerContextImpl context1("foo", true, true, get_request1,
                                        NetworkFilters::Common::Redis::Client::ReadPolicy::Primary);

  EXPECT_EQ(std::optional<uint64_t>(44950), context1.computeHashKey());
  EXPECT_EQ(true, context1.isReadCommand());
  EXPECT_EQ(NetworkFilters::Common::Redis::Client::ReadPolicy::Primary, context1.readPolicy());

  RedisLoadBalancerContextImpl context2("bar", true, true, get_request2,
                                        NetworkFilters::Common::Redis::Client::ReadPolicy::Primary);

  EXPECT_EQ(std::optional<uint64_t>(37829), context2.computeHashKey());
  EXPECT_EQ(true, context2.isReadCommand());
  EXPECT_EQ(NetworkFilters::Common::Redis::Client::ReadPolicy::Primary, context2.readPolicy());

  // Composite write command
  NetworkFilters::Common::Redis::RespValue set_command;
  set_command.type(NetworkFilters::Common::Redis::RespType::SimpleString);
  set_command.asString() = "set";

  NetworkFilters::Common::Redis::RespValue set_request{base, set_command, 1, 2};
  RedisLoadBalancerContextImpl context3("foo", true, true, set_request,
                                        NetworkFilters::Common::Redis::Client::ReadPolicy::Primary);

  EXPECT_EQ(std::optional<uint64_t>(44950), context3.computeHashKey());
  EXPECT_EQ(false, context3.isReadCommand());
  EXPECT_EQ(NetworkFilters::Common::Redis::Client::ReadPolicy::Primary, context3.readPolicy());
}

TEST_F(RedisLoadBalancerContextImplTest, UpperCaseCommand) {
  // Simple read command
  std::vector<NetworkFilters::Common::Redis::RespValue> get_foo(2);
  get_foo[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  get_foo[0].asString() = "GET";
  get_foo[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
  get_foo[1].asString() = "foo";

  NetworkFilters::Common::Redis::RespValue get_request;
  get_request.type(NetworkFilters::Common::Redis::RespType::Array);
  get_request.asArray().swap(get_foo);

  RedisLoadBalancerContextImpl context1("foo", true, true, get_request,
                                        NetworkFilters::Common::Redis::Client::ReadPolicy::Primary);

  EXPECT_EQ(std::optional<uint64_t>(44950), context1.computeHashKey());
  EXPECT_EQ(true, context1.isReadCommand());
  EXPECT_EQ(NetworkFilters::Common::Redis::Client::ReadPolicy::Primary, context1.readPolicy());

  // Simple write command
  std::vector<NetworkFilters::Common::Redis::RespValue> set_foo(3);
  set_foo[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  set_foo[0].asString() = "SET";
  set_foo[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
  set_foo[1].asString() = "foo";
  set_foo[2].type(NetworkFilters::Common::Redis::RespType::BulkString);
  set_foo[2].asString() = "bar";

  NetworkFilters::Common::Redis::RespValue set_request;
  set_request.type(NetworkFilters::Common::Redis::RespType::Array);
  set_request.asArray().swap(set_foo);

  RedisLoadBalancerContextImpl context2("foo", true, true, set_request,
                                        NetworkFilters::Common::Redis::Client::ReadPolicy::Primary);

  EXPECT_EQ(std::optional<uint64_t>(44950), context2.computeHashKey());
  EXPECT_EQ(false, context2.isReadCommand());
  EXPECT_EQ(NetworkFilters::Common::Redis::Client::ReadPolicy::Primary, context2.readPolicy());
}

TEST_F(RedisLoadBalancerContextImplTest, UnsupportedCommand) {
  std::vector<NetworkFilters::Common::Redis::RespValue> unknown(1);
  unknown[0].type(NetworkFilters::Common::Redis::RespType::Integer);
  unknown[0].asInteger() = 1;
  NetworkFilters::Common::Redis::RespValue unknown_request;
  unknown_request.type(NetworkFilters::Common::Redis::RespType::Array);
  unknown_request.asArray().swap(unknown);

  RedisLoadBalancerContextImpl context3("foo", true, true, unknown_request,
                                        NetworkFilters::Common::Redis::Client::ReadPolicy::Primary);

  EXPECT_EQ(std::optional<uint64_t>(44950), context3.computeHashKey());
  EXPECT_EQ(false, context3.isReadCommand());
  EXPECT_EQ(NetworkFilters::Common::Redis::Client::ReadPolicy::Primary, context3.readPolicy());
}

TEST_F(RedisLoadBalancerContextImplTest, EnforceHashTag) {
  std::vector<NetworkFilters::Common::Redis::RespValue> set_foo(3);
  set_foo[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  set_foo[0].asString() = "set";
  set_foo[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
  set_foo[1].asString() = "{foo}bar";
  set_foo[2].type(NetworkFilters::Common::Redis::RespType::BulkString);
  set_foo[2].asString() = "bar";

  NetworkFilters::Common::Redis::RespValue set_request;
  set_request.type(NetworkFilters::Common::Redis::RespType::Array);
  set_request.asArray().swap(set_foo);

  // Enable_hash tagging should be override when is_redis_cluster is true. This is treated like
  // "foo"
  RedisLoadBalancerContextImpl context2("{foo}bar", false, true, set_request,
                                        NetworkFilters::Common::Redis::Client::ReadPolicy::Primary);

  EXPECT_EQ(std::optional<uint64_t>(44950), context2.computeHashKey());
  EXPECT_EQ(false, context2.isReadCommand());
  EXPECT_EQ(NetworkFilters::Common::Redis::Client::ReadPolicy::Primary, context2.readPolicy());
}

TEST_F(RedisLoadBalancerContextImplTest, ClientZone) {
  std::vector<NetworkFilters::Common::Redis::RespValue> get_foo(2);
  get_foo[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  get_foo[0].asString() = "get";
  get_foo[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
  get_foo[1].asString() = "foo";

  NetworkFilters::Common::Redis::RespValue get_request;
  get_request.type(NetworkFilters::Common::Redis::RespType::Array);
  get_request.asArray().swap(get_foo);

  // Test with client zone specified
  RedisLoadBalancerContextImpl context1(
      "foo", true, true, get_request,
      NetworkFilters::Common::Redis::Client::ReadPolicy::LocalZoneAffinity, "us-east-1a");

  EXPECT_EQ("us-east-1a", context1.clientZone());
  EXPECT_EQ(NetworkFilters::Common::Redis::Client::ReadPolicy::LocalZoneAffinity,
            context1.readPolicy());

  // Test with empty client zone
  RedisLoadBalancerContextImpl context2(
      "foo", true, true, get_request,
      NetworkFilters::Common::Redis::Client::ReadPolicy::LocalZoneAffinityReplicasAndPrimary);

  EXPECT_EQ("", context2.clientZone());
  EXPECT_EQ(NetworkFilters::Common::Redis::Client::ReadPolicy::LocalZoneAffinityReplicasAndPrimary,
            context2.readPolicy());
}

// Tests for LOCAL_ZONE_AFFINITY read policy with replicas in the same zone
TEST_F(RedisClusterLoadBalancerTest, LocalZoneAffinityWithLocalReplica) {
  // Setup: primary in zone-a, replica in zone-a (same as client)
  // Hosts must have locality zones set - RedisShard reads zone from host->locality().zone()
  Upstream::HostVector hosts{
      makeHostWithZone("tcp://127.0.0.1:90", "zone-a"), // primary, zone-a
      makeHostWithZone("tcp://127.0.0.1:91", "zone-b"), // primary, zone-b
      makeHostWithZone("tcp://127.0.0.2:90", "zone-a"), // replica for slot 0, zone-a
      makeHostWithZone("tcp://127.0.0.2:91", "zone-b"), // replica for slot 1, zone-b
  };

  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 8000, hosts[0]->address()),
      ClusterSlot(8001, 16383, hosts[1]->address()),
  });
  slots->at(0).addReplica(hosts[2]->address());
  slots->at(1).addReplica(hosts[3]->address());

  Upstream::HostMap all_hosts;
  std::transform(hosts.begin(), hosts.end(), std::inserter(all_hosts, all_hosts.end()), makePair);
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  // Client is in zone-a, should prefer replica in zone-a for slot 0
  const std::vector<std::pair<uint32_t, uint32_t>> expected_assignments = {
      {0, 2},    // slot 0: replica in zone-a
      {8001, 3}, // slot 1: replica in zone-b (no local replica, fall back to any replica)
  };
  validateAssignment(hosts, expected_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::LocalZoneAffinity,
                     "zone-a");
}

// Tests for LOCAL_ZONE_AFFINITY when no local replica exists - should fall back to any replica
TEST_F(RedisClusterLoadBalancerTest, LocalZoneAffinityNoLocalReplica) {
  // Hosts must have locality zones set - RedisShard reads zone from host->locality().zone()
  Upstream::HostVector hosts{
      makeHostWithZone("tcp://127.0.0.1:90", "zone-a"), // primary, zone-a
      makeHostWithZone("tcp://127.0.0.2:90", "zone-b"), // replica, zone-b
  };

  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 16383, hosts[0]->address()),
  });
  slots->at(0).addReplica(hosts[1]->address());

  Upstream::HostMap all_hosts;
  std::transform(hosts.begin(), hosts.end(), std::inserter(all_hosts, all_hosts.end()), makePair);
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  // Client is in zone-c (no local replica), should fall back to any replica
  const std::vector<std::pair<uint32_t, uint32_t>> expected_assignments = {
      {0, 1}, // falls back to replica in zone-b
  };
  validateAssignment(hosts, expected_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::LocalZoneAffinity,
                     "zone-c");
}

// Tests for LOCAL_ZONE_AFFINITY when no replica exists - should fall back to primary
TEST_F(RedisClusterLoadBalancerTest, LocalZoneAffinityNoReplica) {
  // Hosts must have locality zones set - RedisShard reads zone from host->locality().zone()
  Upstream::HostVector hosts{
      makeHostWithZone("tcp://127.0.0.1:90", "zone-a"), // primary only
  };

  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 16383, hosts[0]->address()),
  });

  Upstream::HostMap all_hosts;
  std::transform(hosts.begin(), hosts.end(), std::inserter(all_hosts, all_hosts.end()), makePair);
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  // No replicas, should fall back to primary
  const std::vector<std::pair<uint32_t, uint32_t>> expected_assignments = {
      {0, 0}, // falls back to primary
  };
  validateAssignment(hosts, expected_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::LocalZoneAffinity,
                     "zone-a");
}

// Tests for LOCAL_ZONE_AFFINITY_REPLICAS_AND_PRIMARY with local replica
TEST_F(RedisClusterLoadBalancerTest, LocalZoneAffinityReplicasAndPrimaryWithLocalReplica) {
  // Hosts must have locality zones set - RedisShard reads zone from host->locality().zone()
  Upstream::HostVector hosts{
      makeHostWithZone("tcp://127.0.0.1:90", "zone-a"), // primary, zone-a
      makeHostWithZone("tcp://127.0.0.2:90", "zone-a"), // replica, zone-a (same as client)
  };

  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 16383, hosts[0]->address()),
  });
  slots->at(0).addReplica(hosts[1]->address());

  Upstream::HostMap all_hosts;
  std::transform(hosts.begin(), hosts.end(), std::inserter(all_hosts, all_hosts.end()), makePair);
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  // Client is in zone-a, should prefer replica in zone-a
  const std::vector<std::pair<uint32_t, uint32_t>> expected_assignments = {
      {0, 1}, // replica in zone-a
  };
  validateAssignment(
      hosts, expected_assignments, true,
      NetworkFilters::Common::Redis::Client::ReadPolicy::LocalZoneAffinityReplicasAndPrimary,
      "zone-a");
}

// Tests for LOCAL_ZONE_AFFINITY_REPLICAS_AND_PRIMARY - prefer local primary when no local replica
TEST_F(RedisClusterLoadBalancerTest, LocalZoneAffinityReplicasAndPrimaryLocalPrimary) {
  // Hosts must have locality zones set - RedisShard reads zone from host->locality().zone()
  Upstream::HostVector hosts{
      makeHostWithZone("tcp://127.0.0.1:90", "zone-a"), // primary, zone-a (same as client)
      makeHostWithZone("tcp://127.0.0.2:90", "zone-b"), // replica, zone-b
  };

  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 16383, hosts[0]->address()),
  });
  slots->at(0).addReplica(hosts[1]->address());

  Upstream::HostMap all_hosts;
  std::transform(hosts.begin(), hosts.end(), std::inserter(all_hosts, all_hosts.end()), makePair);
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  // Client is in zone-a, no local replica, but primary is in zone-a
  const std::vector<std::pair<uint32_t, uint32_t>> expected_assignments = {
      {0, 0}, // primary in zone-a (no local replica, but local primary)
  };
  validateAssignment(
      hosts, expected_assignments, true,
      NetworkFilters::Common::Redis::Client::ReadPolicy::LocalZoneAffinityReplicasAndPrimary,
      "zone-a");
}

// Tests for LOCAL_ZONE_AFFINITY_REPLICAS_AND_PRIMARY - fall back to any replica when no local hosts
TEST_F(RedisClusterLoadBalancerTest, LocalZoneAffinityReplicasAndPrimaryFallbackToReplica) {
  // Hosts must have locality zones set - RedisShard reads zone from host->locality().zone()
  Upstream::HostVector hosts{
      makeHostWithZone("tcp://127.0.0.1:90", "zone-a"), // primary, zone-a
      makeHostWithZone("tcp://127.0.0.2:90", "zone-b"), // replica, zone-b
  };

  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 16383, hosts[0]->address()),
  });
  slots->at(0).addReplica(hosts[1]->address());

  Upstream::HostMap all_hosts;
  std::transform(hosts.begin(), hosts.end(), std::inserter(all_hosts, all_hosts.end()), makePair);
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  // Client is in zone-c (no local hosts), should fall back to any replica
  const std::vector<std::pair<uint32_t, uint32_t>> expected_assignments = {
      {0, 1}, // falls back to replica
  };
  validateAssignment(
      hosts, expected_assignments, true,
      NetworkFilters::Common::Redis::Client::ReadPolicy::LocalZoneAffinityReplicasAndPrimary,
      "zone-c");
}

// Tests for LOCAL_ZONE_AFFINITY_REPLICAS_AND_PRIMARY - fall back to primary when no replicas
TEST_F(RedisClusterLoadBalancerTest, LocalZoneAffinityReplicasAndPrimaryNoReplica) {
  // Hosts must have locality zones set - RedisShard reads zone from host->locality().zone()
  Upstream::HostVector hosts{
      makeHostWithZone("tcp://127.0.0.1:90", "zone-a"), // primary only, zone-a
  };

  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 16383, hosts[0]->address()),
  });

  Upstream::HostMap all_hosts;
  std::transform(hosts.begin(), hosts.end(), std::inserter(all_hosts, all_hosts.end()), makePair);
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  // No replicas, should fall back to primary
  const std::vector<std::pair<uint32_t, uint32_t>> expected_assignments = {
      {0, 0}, // falls back to primary
  };
  validateAssignment(
      hosts, expected_assignments, true,
      NetworkFilters::Common::Redis::Client::ReadPolicy::LocalZoneAffinityReplicasAndPrimary,
      "zone-c");
}

// Tests for LOCAL_ZONE_AFFINITY when zone discovery fails - hosts have no zones set
// Should fall back to any-replica behavior (same as PreferReplica)
TEST_F(RedisClusterLoadBalancerTest, LocalZoneAffinityZoneDiscoveryFailure) {
  // Hosts without zone set - simulates zone discovery failure scenario
  Upstream::HostVector hosts{
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"), // primary, no zone
      Upstream::makeTestHost(info_, "tcp://127.0.0.2:90"), // replica, no zone
  };

  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 16383, hosts[0]->address()),
  });
  slots->at(0).addReplica(hosts[1]->address());

  Upstream::HostMap all_hosts;
  std::transform(hosts.begin(), hosts.end(), std::inserter(all_hosts, all_hosts.end()), makePair);
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  // Client has a zone, but hosts don't - should fall back to any replica
  const std::vector<std::pair<uint32_t, uint32_t>> expected_assignments = {
      {0, 1}, // falls back to any replica since no host zones match
  };
  validateAssignment(hosts, expected_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::LocalZoneAffinity,
                     "zone-a");
}

// Tests for LOCAL_ZONE_AFFINITY_REPLICAS_AND_PRIMARY - unhealthy local primary falls through
TEST_F(RedisClusterLoadBalancerTest, LocalZoneAffinityReplicasAndPrimaryUnhealthyLocalPrimary) {
  Upstream::HostVector hosts{
      makeHostWithZone("tcp://127.0.0.1:90", "zone-a"), // primary, zone-a (same as client)
      makeHostWithZone("tcp://127.0.0.2:90", "zone-b"), // replica, zone-b
  };

  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 16383, hosts[0]->address()),
  });
  slots->at(0).addReplica(hosts[1]->address());

  Upstream::HostMap all_hosts;
  std::transform(hosts.begin(), hosts.end(), std::inserter(all_hosts, all_hosts.end()), makePair);
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  // Mark primary unhealthy - should skip local primary even though zone matches
  hosts[0]->healthFlagSet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC);
  factory_->onHostHealthUpdate();

  // Client is in zone-a, local primary is unhealthy, no local replica → fall back to any replica
  const std::vector<std::pair<uint32_t, uint32_t>> expected_assignments = {
      {0, 1}, // falls back to replica in zone-b
  };
  validateAssignment(
      hosts, expected_assignments, true,
      NetworkFilters::Common::Redis::Client::ReadPolicy::LocalZoneAffinityReplicasAndPrimary,
      "zone-a");
}

// Tests for LOCAL_ZONE_AFFINITY with empty client zone - should behave like PreferReplica
TEST_F(RedisClusterLoadBalancerTest, LocalZoneAffinityEmptyClientZone) {
  Upstream::HostVector hosts{
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:90"), // primary
      Upstream::makeTestHost(info_, "tcp://127.0.0.2:90"), // replica
  };

  ClusterSlotsPtr slots = std::make_unique<std::vector<ClusterSlot>>(std::vector<ClusterSlot>{
      ClusterSlot(0, 16383, hosts[0]->address()),
  });
  slots->at(0).addReplica(hosts[1]->address());

  Upstream::HostMap all_hosts;
  std::transform(hosts.begin(), hosts.end(), std::inserter(all_hosts, all_hosts.end()), makePair);
  init();
  factory_->onClusterSlotUpdate(std::move(slots), all_hosts);

  // Empty client zone, should fall back to any replica
  const std::vector<std::pair<uint32_t, uint32_t>> expected_assignments = {
      {0, 1}, // any replica
  };
  validateAssignment(hosts, expected_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::LocalZoneAffinity, "");
}

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
