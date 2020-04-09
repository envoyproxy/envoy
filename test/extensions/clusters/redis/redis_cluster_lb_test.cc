#include <memory>

#include "source/extensions/clusters/redis/redis_cluster_lb.h"

#include "extensions/filters/network/common/redis/client.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/upstream/mocks.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

class TestLoadBalancerContext : public RedisLoadBalancerContext,
                                public Upstream::LoadBalancerContextBase {
public:
  TestLoadBalancerContext(uint64_t hash_key, bool is_read,
                          NetworkFilters::Common::Redis::Client::ReadPolicy read_policy)
      : hash_key_(hash_key), is_read_(is_read), read_policy_(read_policy) {}

  TestLoadBalancerContext(absl::optional<uint64_t> hash) : hash_key_(hash) {}

  // Upstream::LoadBalancerContext
  absl::optional<uint64_t> computeHashKey() override { return hash_key_; }

  bool isReadCommand() const override { return is_read_; };
  NetworkFilters::Common::Redis::Client::ReadPolicy readPolicy() const override {
    return read_policy_;
  };

  absl::optional<uint64_t> hash_key_;
  bool is_read_;
  NetworkFilters::Common::Redis::Client::ReadPolicy read_policy_;
};

class RedisClusterLoadBalancerTest : public testing::Test {
public:
  RedisClusterLoadBalancerTest() = default;

  void init() {
    factory_ = std::make_shared<RedisClusterLoadBalancerFactory>(random_);
    lb_ = std::make_unique<RedisClusterThreadAwareLoadBalancer>(factory_);
    lb_->initialize();
    factory_->onHostHealthUpdate();
  }

  void validateAssignment(Upstream::HostVector& hosts,
                          const std::vector<std::pair<uint32_t, uint32_t>>& expected_assignments,
                          bool read_command = false,
                          NetworkFilters::Common::Redis::Client::ReadPolicy read_policy =
                              NetworkFilters::Common::Redis::Client::ReadPolicy::Master) {

    Upstream::LoadBalancerPtr lb = lb_->factory()->create();
    for (auto& assignment : expected_assignments) {
      TestLoadBalancerContext context(assignment.first, read_command, read_policy);
      auto host = lb->chooseHost(&context);
      EXPECT_FALSE(host == nullptr);
      EXPECT_EQ(hosts[assignment.second]->address()->asString(), host->address()->asString());
    }
  }

  static std::pair<std::string, Upstream::HostSharedPtr> makePair(Upstream::HostSharedPtr host) {
    return std::make_pair(host->address()->asString(), std::move(host));
  }

  Upstream::HostMap generateHostMap(Upstream::HostVector& hosts) {
    Upstream::HostMap map;
    std::transform(hosts.begin(), hosts.end(), std::inserter(map, map.end()), makePair);
    return map;
  }

  std::shared_ptr<RedisClusterLoadBalancerFactory> factory_;
  std::unique_ptr<RedisClusterThreadAwareLoadBalancer> lb_;
  std::shared_ptr<Upstream::MockClusterInfo> info_{new NiceMock<Upstream::MockClusterInfo>()};
  NiceMock<Runtime::MockRandomGenerator> random_;
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
  EXPECT_EQ(nullptr, lb_->factory()->create()->chooseHost(nullptr));
};

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
  TestLoadBalancerContext context(absl::nullopt);
  EXPECT_EQ(nullptr, lb_->factory()->create()->chooseHost(&context));
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

  const std::vector<std::pair<uint32_t, uint32_t>> master_assignments = {
      {0, 0}, {1100, 0}, {2000, 0}, {18382, 0}, {2001, 1}, {2100, 1}, {16383, 1}, {19382, 1}};
  validateAssignment(hosts, master_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Master);
  validateAssignment(hosts, master_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::PreferMaster);

  ON_CALL(random_, random()).WillByDefault(Return(0));
  validateAssignment(hosts, master_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Any);
  ON_CALL(random_, random()).WillByDefault(Return(1));
  validateAssignment(hosts, replica_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Any);
}

TEST_F(RedisClusterLoadBalancerTest, ReadStrategiesUnhealthyMaster) {
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
  const std::vector<std::pair<uint32_t, uint32_t>> master_assignments = {
      {0, 0}, {1100, 0}, {2000, 0}, {18382, 0}, {2001, 1}, {2100, 1}, {16383, 1}, {19382, 1}};

  validateAssignment(hosts, replica_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Replica);
  validateAssignment(hosts, replica_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::PreferReplica);
  validateAssignment(hosts, master_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Master);
  validateAssignment(hosts, replica_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::PreferMaster);

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
  const std::vector<std::pair<uint32_t, uint32_t>> master_assignments = {
      {0, 0}, {1100, 0}, {2000, 0}, {18382, 0}, {2001, 1}, {2100, 1}, {16383, 1}, {19382, 1}};

  validateAssignment(hosts, replica_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Replica);
  validateAssignment(hosts, master_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::PreferReplica);
  validateAssignment(hosts, master_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Master);
  validateAssignment(hosts, master_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::PreferMaster);

  ON_CALL(random_, random()).WillByDefault(Return(0));
  validateAssignment(hosts, master_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Any);
  ON_CALL(random_, random()).WillByDefault(Return(1));
  validateAssignment(hosts, master_assignments, true,
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
  const std::vector<std::pair<uint32_t, uint32_t>> master_assignments = {
      {0, 0}, {1100, 0}, {2000, 0}, {18382, 0}, {2001, 1}, {2100, 1}, {16383, 1}, {19382, 1}};
  validateAssignment(hosts, master_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Master);
  validateAssignment(hosts, master_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::PreferMaster);
  validateAssignment(hosts, master_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::Any);
  validateAssignment(hosts, master_assignments, true,
                     NetworkFilters::Common::Redis::Client::ReadPolicy::PreferReplica);

  Upstream::LoadBalancerPtr lb = lb_->factory()->create();
  TestLoadBalancerContext context(1100, true,
                                  NetworkFilters::Common::Redis::Client::ReadPolicy::Replica);
  auto host = lb->chooseHost(&context);
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

TEST_F(RedisClusterLoadBalancerTest, ClusterSlotNoUpdate) {
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

  // A list of (hash: host_index) pair.
  const std::vector<std::pair<uint32_t, uint32_t>> expected_assignments = {
      {100, 0}, {1100, 1}, {2100, 2}};

  init();
  EXPECT_EQ(true, factory_->onClusterSlotUpdate(std::move(slots), all_hosts));
  validateAssignment(hosts, expected_assignments);

  // Calling cluster slot update without change should not change assignment.
  std::vector<ClusterSlot> updated_slot{
      ClusterSlot(0, 1000, hosts[0]->address()),
      ClusterSlot(1001, 2000, hosts[1]->address()),
      ClusterSlot(2001, 16383, hosts[2]->address()),
  };
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
                                        NetworkFilters::Common::Redis::Client::ReadPolicy::Master);

  EXPECT_EQ(absl::optional<uint64_t>(44950), context1.computeHashKey());
  EXPECT_EQ(true, context1.isReadCommand());
  EXPECT_EQ(NetworkFilters::Common::Redis::Client::ReadPolicy::Master, context1.readPolicy());

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
                                        NetworkFilters::Common::Redis::Client::ReadPolicy::Master);

  EXPECT_EQ(absl::optional<uint64_t>(44950), context2.computeHashKey());
  EXPECT_EQ(false, context2.isReadCommand());
  EXPECT_EQ(NetworkFilters::Common::Redis::Client::ReadPolicy::Master, context2.readPolicy());
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
                                        NetworkFilters::Common::Redis::Client::ReadPolicy::Master);

  EXPECT_EQ(absl::optional<uint64_t>(44950), context1.computeHashKey());
  EXPECT_EQ(true, context1.isReadCommand());
  EXPECT_EQ(NetworkFilters::Common::Redis::Client::ReadPolicy::Master, context1.readPolicy());

  RedisLoadBalancerContextImpl context2("bar", true, true, get_request2,
                                        NetworkFilters::Common::Redis::Client::ReadPolicy::Master);

  EXPECT_EQ(absl::optional<uint64_t>(37829), context2.computeHashKey());
  EXPECT_EQ(true, context2.isReadCommand());
  EXPECT_EQ(NetworkFilters::Common::Redis::Client::ReadPolicy::Master, context2.readPolicy());

  // Composite write command
  NetworkFilters::Common::Redis::RespValue set_command;
  set_command.type(NetworkFilters::Common::Redis::RespType::SimpleString);
  set_command.asString() = "set";

  NetworkFilters::Common::Redis::RespValue set_request{base, set_command, 1, 2};
  RedisLoadBalancerContextImpl context3("foo", true, true, set_request,
                                        NetworkFilters::Common::Redis::Client::ReadPolicy::Master);

  EXPECT_EQ(absl::optional<uint64_t>(44950), context3.computeHashKey());
  EXPECT_EQ(false, context3.isReadCommand());
  EXPECT_EQ(NetworkFilters::Common::Redis::Client::ReadPolicy::Master, context3.readPolicy());
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
                                        NetworkFilters::Common::Redis::Client::ReadPolicy::Master);

  EXPECT_EQ(absl::optional<uint64_t>(44950), context1.computeHashKey());
  EXPECT_EQ(true, context1.isReadCommand());
  EXPECT_EQ(NetworkFilters::Common::Redis::Client::ReadPolicy::Master, context1.readPolicy());

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
                                        NetworkFilters::Common::Redis::Client::ReadPolicy::Master);

  EXPECT_EQ(absl::optional<uint64_t>(44950), context2.computeHashKey());
  EXPECT_EQ(false, context2.isReadCommand());
  EXPECT_EQ(NetworkFilters::Common::Redis::Client::ReadPolicy::Master, context2.readPolicy());
}

TEST_F(RedisLoadBalancerContextImplTest, UnsupportedCommand) {
  std::vector<NetworkFilters::Common::Redis::RespValue> unknown(1);
  unknown[0].type(NetworkFilters::Common::Redis::RespType::Integer);
  unknown[0].asInteger() = 1;
  NetworkFilters::Common::Redis::RespValue unknown_request;
  unknown_request.type(NetworkFilters::Common::Redis::RespType::Array);
  unknown_request.asArray().swap(unknown);

  RedisLoadBalancerContextImpl context3("foo", true, true, unknown_request,
                                        NetworkFilters::Common::Redis::Client::ReadPolicy::Master);

  EXPECT_EQ(absl::optional<uint64_t>(44950), context3.computeHashKey());
  EXPECT_EQ(false, context3.isReadCommand());
  EXPECT_EQ(NetworkFilters::Common::Redis::Client::ReadPolicy::Master, context3.readPolicy());
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
                                        NetworkFilters::Common::Redis::Client::ReadPolicy::Master);

  EXPECT_EQ(absl::optional<uint64_t>(44950), context2.computeHashKey());
  EXPECT_EQ(false, context2.isReadCommand());
  EXPECT_EQ(NetworkFilters::Common::Redis::Client::ReadPolicy::Master, context2.readPolicy());
}

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
