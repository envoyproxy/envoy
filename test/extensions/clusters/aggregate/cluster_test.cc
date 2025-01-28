#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.pb.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.pb.validate.h"

#include "source/common/singleton/manager_impl.h"
#include "source/extensions/clusters/aggregate/cluster.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/load_balancer.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"

using testing::Eq;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

namespace {
const std::string primary_name("primary");
const std::string secondary_name("secondary");
} // namespace

class AggregateClusterTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  AggregateClusterTest()
      : stat_names_(server_context_.store_.symbolTable()),
        traffic_stats_(Upstream::ClusterInfoImpl::generateStats(server_context_.store_.rootScope(),
                                                                stat_names_, false)) {
    ON_CALL(*primary_info_, name()).WillByDefault(ReturnRef(primary_name));
    ON_CALL(*secondary_info_, name()).WillByDefault(ReturnRef(secondary_name));

    ON_CALL(server_context_, api()).WillByDefault(ReturnRef(*api_));
  }

  Upstream::HostVector setupHostSet(Upstream::ClusterInfoConstSharedPtr cluster, int healthy_hosts,
                                    int degraded_hosts, int unhealthy_hosts, uint32_t priority) {
    Upstream::HostVector hosts;
    for (int i = 0; i < healthy_hosts; ++i) {
      hosts.emplace_back(
          Upstream::makeTestHost(cluster, "tcp://127.0.0.1:80", simTime(), 1, priority));
    }

    for (int i = 0; i < degraded_hosts; ++i) {
      Upstream::HostSharedPtr host =
          Upstream::makeTestHost(cluster, "tcp://127.0.0.2:80", simTime(), 1, priority);
      host->healthFlagSet(Upstream::HostImpl::HealthFlag::DEGRADED_ACTIVE_HC);
      hosts.emplace_back(host);
    }

    for (int i = 0; i < unhealthy_hosts; ++i) {
      Upstream::HostSharedPtr host =
          Upstream::makeTestHost(cluster, "tcp://127.0.0.3:80", simTime(), 1, priority);
      host->healthFlagSet(Upstream::HostImpl::HealthFlag::FAILED_ACTIVE_HC);
      hosts.emplace_back(host);
    }

    return hosts;
  }

  void setupPrimary(int priority, int healthy_hosts, int degraded_hosts, int unhealthy_hosts) {
    auto hosts =
        setupHostSet(primary_info_, healthy_hosts, degraded_hosts, unhealthy_hosts, priority);
    primary_ps_.updateHosts(
        priority,
        Upstream::HostSetImpl::partitionHosts(std::make_shared<Upstream::HostVector>(hosts),
                                              Upstream::HostsPerLocalityImpl::empty()),
        nullptr, hosts, {}, 123, absl::nullopt, 100);
  }

  void setupSecondary(int priority, int healthy_hosts, int degraded_hosts, int unhealthy_hosts) {
    auto hosts =
        setupHostSet(secondary_info_, healthy_hosts, degraded_hosts, unhealthy_hosts, priority);
    secondary_ps_.updateHosts(
        priority,
        Upstream::HostSetImpl::partitionHosts(std::make_shared<Upstream::HostVector>(hosts),
                                              Upstream::HostsPerLocalityImpl::empty()),
        nullptr, hosts, {}, 123, absl::nullopt, 100);
  }

  void setupPrioritySet() {
    setupPrimary(0, 1, 1, 1);
    setupPrimary(1, 2, 2, 2);
    setupSecondary(0, 2, 2, 2);
    setupSecondary(1, 1, 1, 1);
  }

  void initialize(const std::string& yaml_config) {
    envoy::config::cluster::v3::Cluster cluster_config =
        Upstream::parseClusterFromV3Yaml(yaml_config);
    envoy::extensions::clusters::aggregate::v3::ClusterConfig config;
    THROW_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
        cluster_config.cluster_type().typed_config(), ProtobufMessage::getStrictValidationVisitor(),
        config));

    Envoy::Upstream::ClusterFactoryContextImpl factory_context(
        server_context_, server_context_.cluster_manager_, nullptr, ssl_context_manager_, nullptr,
        false);

    absl::Status creation_status = absl::OkStatus();
    cluster_ = std::shared_ptr<Cluster>(
        new Cluster(cluster_config, config, factory_context, creation_status));
    THROW_IF_NOT_OK(creation_status);

    server_context_.cluster_manager_.initializeThreadLocalClusters({"primary", "secondary"});
    primary_.cluster_.info_->name_ = "primary";
    EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster(Eq("primary")))
        .WillRepeatedly(Return(&primary_));
    secondary_.cluster_.info_->name_ = "secondary";
    EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster(Eq("secondary")))
        .WillRepeatedly(Return(&secondary_));
    ON_CALL(primary_, prioritySet()).WillByDefault(ReturnRef(primary_ps_));
    ON_CALL(secondary_, prioritySet()).WillByDefault(ReturnRef(secondary_ps_));

    setupPrioritySet();

    EXPECT_CALL(*primary_info_, resourceManager(Upstream::ResourcePriority::Default)).Times(0);
    EXPECT_CALL(*secondary_info_, resourceManager(Upstream::ResourcePriority::Default)).Times(0);

    ON_CALL(primary_, loadBalancer()).WillByDefault(ReturnRef(primary_load_balancer_));
    ON_CALL(secondary_, loadBalancer()).WillByDefault(ReturnRef(secondary_load_balancer_));

    thread_aware_lb_ = std::make_unique<AggregateThreadAwareLoadBalancer>(*cluster_);
    lb_factory_ = thread_aware_lb_->factory();
    lb_ = lb_factory_->create(lb_params_);
  }

  Stats::Gauge& getCircuitBreakersStatByPriority(std::string priority, std::string stat) {
    std::string stat_name = "circuit_breakers." + priority + "." + stat;
    Stats::StatNameManagedStorage statStore(stat_name,
                                            cluster_->info()->statsScope().symbolTable());
    return cluster_->info()->statsScope().gaugeFromStatName(statStore.statName(),
                                                            Stats::Gauge::ImportMode::Accumulate);
  }

  void assertResourceManagerStat(ResourceLimit& resource, Stats::Gauge& remaining,
                                 Stats::Gauge& open, bool expected_can_create,
                                 unsigned int expected_count, unsigned int expected_remaining,
                                 unsigned int expected_open) {
    EXPECT_EQ(expected_can_create, resource.canCreate());
    EXPECT_EQ(expected_count, resource.count());
    EXPECT_EQ(expected_remaining, remaining.value());
    EXPECT_EQ(expected_open, open.value());
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  Ssl::MockContextManager ssl_context_manager_;

  NiceMock<Random::MockRandomGenerator> random_;
  Api::ApiPtr api_{Api::createApiForTest(server_context_.store_, random_)};

  std::shared_ptr<Cluster> cluster_;
  Upstream::ThreadAwareLoadBalancerPtr thread_aware_lb_;
  Upstream::LoadBalancerFactorySharedPtr lb_factory_;
  Upstream::LoadBalancerPtr lb_;
  Upstream::ClusterTrafficStatNames stat_names_;
  Upstream::DeferredCreationCompatibleClusterTrafficStats traffic_stats_;
  std::shared_ptr<Upstream::MockClusterInfo> primary_info_{
      new NiceMock<Upstream::MockClusterInfo>()};
  std::shared_ptr<Upstream::MockClusterInfo> secondary_info_{
      new NiceMock<Upstream::MockClusterInfo>()};
  NiceMock<Upstream::MockThreadLocalCluster> primary_, secondary_;
  Upstream::PrioritySetImpl primary_ps_, secondary_ps_;
  NiceMock<Upstream::MockLoadBalancer> primary_load_balancer_, secondary_load_balancer_;

  // Just use this as parameters of create() method but thread aware load balancer will not use it.
  NiceMock<Upstream::MockPrioritySet> worker_priority_set_;
  Upstream::LoadBalancerParams lb_params_{worker_priority_set_, {}};

  const std::string default_yaml_config_ = R"EOF(
    name: aggregate_cluster
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.aggregate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.aggregate.v3.ClusterConfig
        clusters:
        - primary
        - secondary
)EOF";
}; // namespace Aggregate

TEST_F(AggregateClusterTest, CircuitBreakerDefaultsTest) {
  initialize(default_yaml_config_);

  Upstream::ResourceManager& resource_manager =
      cluster_->info()->resourceManager(Upstream::ResourcePriority::Default);

  for (uint64_t i = 0; i < resource_manager.connections().max(); ++i) {
    resource_manager.connections().inc();
  }
  EXPECT_EQ(resource_manager.connections().count(), resource_manager.connections().max());
  EXPECT_FALSE(resource_manager.connections().canCreate());

  for (uint64_t i = 0; i < resource_manager.pendingRequests().max(); ++i) {
    resource_manager.pendingRequests().inc();
  }
  EXPECT_EQ(resource_manager.pendingRequests().count(), resource_manager.pendingRequests().max());
  EXPECT_FALSE(resource_manager.pendingRequests().canCreate());

  for (uint64_t i = 0; i < resource_manager.requests().max(); ++i) {
    resource_manager.requests().inc();
  }
  EXPECT_EQ(resource_manager.requests().count(), resource_manager.requests().max());
  EXPECT_FALSE(resource_manager.requests().canCreate());

  for (uint64_t i = 0; i < resource_manager.retries().max(); ++i) {
    resource_manager.retries().inc();
  }
  EXPECT_EQ(resource_manager.retries().count(), resource_manager.retries().max());
  EXPECT_FALSE(resource_manager.retries().canCreate());
}

TEST_F(AggregateClusterTest, CircuitBreakerMaxConnectionsTest) {
  const std::string yaml_config = R"EOF(
    name: aggregate_cluster
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    circuit_breakers:
      thresholds:
      - priority: DEFAULT
        max_connections: 1
        track_remaining: true
    cluster_type:
      name: envoy.clusters.aggregate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.aggregate.v3.ClusterConfig
        clusters:
        - primary
        - secondary
)EOF";

  initialize(yaml_config);

  // resource manager for the DEFAULT priority (see the yaml config above)
  Upstream::ResourceManager& resource_manager =
      cluster_->info()->resourceManager(Upstream::ResourcePriority::Default);

  // get the circuit breaker statistics we are interested in, to assert against
  Stats::Gauge& cx_open = getCircuitBreakersStatByPriority("default", "cx_open");
  Stats::Gauge& remaining_cx = getCircuitBreakersStatByPriority("default", "remaining_cx");

  // check the yaml config is set correctly,
  // we should have a maximum of 1 connection available to use
  EXPECT_EQ(1U, resource_manager.connections().max());

  // test the specific stat's remaining value and it's related circuit breaker's state
  // eg. max_connections, the remaining connections, and the connections circuit breaker state
  assertResourceManagerStat(resource_manager.connections(), remaining_cx, cx_open, true, 0U, 1U,
                            0U);

  // create that one connection
  resource_manager.connections().inc();

  assertResourceManagerStat(resource_manager.connections(), remaining_cx, cx_open, false, 1U, 0U,
                            1U);

  // remove that one connection
  resource_manager.connections().dec();

  assertResourceManagerStat(resource_manager.connections(), remaining_cx, cx_open, true, 0U, 1U,
                            0U);
}

TEST_F(AggregateClusterTest, CircuitBreakerMaxConnectionsPriorityTest) {
  const std::string yaml_config = R"EOF(
    name: aggregate_cluster
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    circuit_breakers:
      thresholds:
      - priority: DEFAULT
        max_connections: 1
        track_remaining: true
      - priority: HIGH
        max_connections: 1
        track_remaining: true
    cluster_type:
      name: envoy.clusters.aggregate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.aggregate.v3.ClusterConfig
        clusters:
        - primary
        - secondary
)EOF";

  initialize(yaml_config);

  Upstream::ResourceManager& resource_manager_default =
      cluster_->info()->resourceManager(Upstream::ResourcePriority::Default);
  Upstream::ResourceManager& resource_manager_high =
      cluster_->info()->resourceManager(Upstream::ResourcePriority::High);

  Stats::Gauge& cx_open_default = getCircuitBreakersStatByPriority("default", "cx_open");
  Stats::Gauge& remaining_cx_default = getCircuitBreakersStatByPriority("default", "remaining_cx");
  Stats::Gauge& cx_open_high = getCircuitBreakersStatByPriority("high", "cx_open");
  Stats::Gauge& remaining_cx_high = getCircuitBreakersStatByPriority("high", "remaining_cx");

  // check the initial max_connections for DEFAULT priority matches the config
  EXPECT_EQ(1U, resource_manager_default.connections().max());
  assertResourceManagerStat(resource_manager_default.connections(), remaining_cx_default,
                            cx_open_default, true, 0U, 1U, 0U);

  // check the initial max_connections for HIGH priority matches the config
  EXPECT_EQ(1U, resource_manager_high.connections().max());
  assertResourceManagerStat(resource_manager_high.connections(), remaining_cx_high, cx_open_high,
                            true, 0U, 1U, 0U);

  // add a connection to DEFAULT priority
  resource_manager_default.connections().inc();
  // check the state of DEFAULT priority circuit breaker state and statistics
  assertResourceManagerStat(resource_manager_default.connections(), remaining_cx_default,
                            cx_open_default, false, 1U, 0U, 1U);
  // check the HIGH priority circuit breaker state and statistics
  assertResourceManagerStat(resource_manager_high.connections(), remaining_cx_high, cx_open_high,
                            true, 0U, 1U, 0U);

  // remove the connection from DEFAULT priority
  resource_manager_default.connections().dec();
  // check the DEFAULT priority circuit breaker state and statistics
  assertResourceManagerStat(resource_manager_default.connections(), remaining_cx_default,
                            cx_open_default, true, 0U, 1U, 0U);
  // check the HIGH priority circuit breaker state and statistics
  assertResourceManagerStat(resource_manager_high.connections(), remaining_cx_high, cx_open_high,
                            true, 0U, 1U, 0U);

  // add a connection to HIGH priority
  resource_manager_high.connections().inc();
  // check the HIGH priority circuit breaker state and statistics
  assertResourceManagerStat(resource_manager_high.connections(), remaining_cx_high, cx_open_high,
                            false, 1U, 0U, 1U);
  // check the DEFAULT priority circuit breaker state and statistics
  assertResourceManagerStat(resource_manager_default.connections(), remaining_cx_default,
                            cx_open_default, true, 0U, 1U, 0U);

  // remove the connection from HIGH priority
  resource_manager_high.connections().dec();
  // check the HIGH priority circuit breaker state and statistics
  assertResourceManagerStat(resource_manager_high.connections(), remaining_cx_high, cx_open_high,
                            true, 0U, 1U, 0U);
  // check the DEFAULT priority circuit breaker and statistics
  assertResourceManagerStat(resource_manager_default.connections(), remaining_cx_default,
                            cx_open_default, true, 0U, 1U, 0U);
}

TEST_F(AggregateClusterTest, CircuitBreakerMaxPendingRequestsTest) {
  const std::string yaml_config = R"EOF(
    name: aggregate_cluster
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    circuit_breakers:
      thresholds:
      - priority: DEFAULT
        max_pending_requests: 1
        track_remaining: true
    cluster_type:
      name: envoy.clusters.aggregate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.aggregate.v3.ClusterConfig
        clusters:
        - primary
        - secondary
)EOF";

  initialize(yaml_config);

  Upstream::ResourceManager& resource_manager =
      cluster_->info()->resourceManager(Upstream::ResourcePriority::Default);

  Stats::Gauge& rq_pending_open = getCircuitBreakersStatByPriority("default", "rq_pending_open");
  Stats::Gauge& remaining_pending =
      getCircuitBreakersStatByPriority("default", "remaining_pending");

  EXPECT_EQ(1U, resource_manager.pendingRequests().max());

  assertResourceManagerStat(resource_manager.pendingRequests(), remaining_pending, rq_pending_open,
                            true, 0U, 1U, 0U);

  resource_manager.pendingRequests().inc();

  assertResourceManagerStat(resource_manager.pendingRequests(), remaining_pending, rq_pending_open,
                            false, 1U, 0U, 1U);

  resource_manager.pendingRequests().dec();

  assertResourceManagerStat(resource_manager.pendingRequests(), remaining_pending, rq_pending_open,
                            true, 0U, 1U, 0U);
}

TEST_F(AggregateClusterTest, CircuitBreakerMaxRequestsTest) {
  const std::string yaml_config = R"EOF(
    name: aggregate_cluster
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    circuit_breakers:
      thresholds:
      - priority: DEFAULT
        max_requests: 1
        track_remaining: true
    cluster_type:
      name: envoy.clusters.aggregate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.aggregate.v3.ClusterConfig
        clusters:
        - primary
        - secondary
)EOF";

  initialize(yaml_config);

  Upstream::ResourceManager& resource_manager =
      cluster_->info()->resourceManager(Upstream::ResourcePriority::Default);

  Stats::Gauge& rq_open = getCircuitBreakersStatByPriority("default", "rq_open");
  Stats::Gauge& remaining_rq = getCircuitBreakersStatByPriority("default", "remaining_rq");

  EXPECT_EQ(1U, resource_manager.requests().max());

  assertResourceManagerStat(resource_manager.requests(), remaining_rq, rq_open, true, 0U, 1U, 0U);

  resource_manager.requests().inc();

  assertResourceManagerStat(resource_manager.requests(), remaining_rq, rq_open, false, 1U, 0U, 1U);

  resource_manager.requests().dec();

  assertResourceManagerStat(resource_manager.requests(), remaining_rq, rq_open, true, 0U, 1U, 0U);
}

TEST_F(AggregateClusterTest, CircuitBreakerMaxRetriesTest) {
  const std::string yaml_config = R"EOF(
    name: aggregate_cluster
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    circuit_breakers:
      thresholds:
      - priority: DEFAULT
        max_retries: 1
        track_remaining: true
    cluster_type:
      name: envoy.clusters.aggregate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.aggregate.v3.ClusterConfig
        clusters:
        - primary
        - secondary
)EOF";

  initialize(yaml_config);

  Upstream::ResourceManager& resource_manager =
      cluster_->info()->resourceManager(Upstream::ResourcePriority::Default);

  Stats::Gauge& rq_retry_open = getCircuitBreakersStatByPriority("default", "rq_retry_open");
  Stats::Gauge& remaining_retries =
      getCircuitBreakersStatByPriority("default", "remaining_retries");

  EXPECT_EQ(1U, resource_manager.retries().max());

  assertResourceManagerStat(resource_manager.retries(), remaining_retries, rq_retry_open, true, 0U,
                            1U, 0U);

  resource_manager.retries().inc();

  assertResourceManagerStat(resource_manager.retries(), remaining_retries, rq_retry_open, false, 1U,
                            0U, 1U);

  resource_manager.retries().dec();

  assertResourceManagerStat(resource_manager.retries(), remaining_retries, rq_retry_open, true, 0U,
                            1U, 0U);
}

TEST_F(AggregateClusterTest, CircuitBreakerMaxConnectionPoolsTest) {
  const std::string yaml_config = R"EOF(
    name: aggregate_cluster
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    circuit_breakers:
      thresholds:
      - priority: DEFAULT
        max_connection_pools: 1
        track_remaining: true
    cluster_type:
      name: envoy.clusters.aggregate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.aggregate.v3.ClusterConfig
        clusters:
        - primary
        - secondary
)EOF";

  initialize(yaml_config);

  Upstream::ResourceManager& resource_manager =
      cluster_->info()->resourceManager(Upstream::ResourcePriority::Default);

  Stats::Gauge& cx_pool_open = getCircuitBreakersStatByPriority("default", "cx_pool_open");
  Stats::Gauge& remaining_cx_pools =
      getCircuitBreakersStatByPriority("default", "remaining_cx_pools");

  EXPECT_EQ(1U, resource_manager.connectionPools().max());

  assertResourceManagerStat(resource_manager.connectionPools(), remaining_cx_pools, cx_pool_open,
                            true, 0U, 1U, 0U);

  resource_manager.connectionPools().inc();

  assertResourceManagerStat(resource_manager.connectionPools(), remaining_cx_pools, cx_pool_open,
                            false, 1U, 0U, 1U);

  resource_manager.connectionPools().dec();

  assertResourceManagerStat(resource_manager.connectionPools(), remaining_cx_pools, cx_pool_open,
                            true, 0U, 1U, 0U);
}

TEST_F(AggregateClusterTest, LoadBalancerTest) {
  initialize(default_yaml_config_);
  // Health value:
  // Cluster 1:
  //     Priority 0: 33.3%
  //     Priority 1: 33.3%
  // Cluster 2:
  //     Priority 0: 33.3%
  //     Priority 1: 33.3%
  Upstream::HostSharedPtr host =
      Upstream::makeTestHost(primary_info_, "tcp://127.0.0.1:80", simTime());
  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Invoke([host] {
    return Upstream::HostSelectionResponse{host};
  }));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Invoke([] {
    return Upstream::HostSelectionResponse{nullptr};
  }));

  for (int i = 0; i <= 65; ++i) {
    EXPECT_CALL(random_, random()).WillRepeatedly(Return(i));
    EXPECT_TRUE(lb_->peekAnotherHost(nullptr) == nullptr);
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr).host;
    OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetime_callbacks =
        lb_->lifetimeCallbacks();
    EXPECT_FALSE(lifetime_callbacks.has_value());
    std::vector<uint8_t> hash_key = {1, 2, 3};
    absl::optional<Upstream::SelectedPoolAndConnection> selection =
        lb_->selectExistingConnection(nullptr, *host, hash_key);
    EXPECT_FALSE(selection.has_value());
    EXPECT_EQ(host.get(), target.get());
  }

  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Invoke([] {
    return Upstream::HostSelectionResponse{nullptr};
  }));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Invoke([host] {
    return Upstream::HostSelectionResponse{host};
  }));
  for (int i = 66; i < 100; ++i) {
    EXPECT_CALL(random_, random()).WillRepeatedly(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr).host;
    EXPECT_EQ(host.get(), target.get());
  }

  // Set up the HostSet with 1 healthy, 1 degraded and 2 unhealthy.
  setupPrimary(0, 1, 1, 2);

  // Health value:
  // Cluster 1:
  //     Priority 0: 25%
  //     Priority 1: 33.3%
  // Cluster 2:
  //     Priority 0: 33.3%
  //     Priority 1: 33.3%
  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Invoke([host] {
    return Upstream::HostSelectionResponse{host};
  }));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Invoke([] {
    return Upstream::HostSelectionResponse{nullptr};
  }));

  for (int i = 0; i <= 57; ++i) {
    EXPECT_CALL(random_, random()).WillRepeatedly(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr).host;
    EXPECT_EQ(host.get(), target.get());
  }

  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Invoke([] {
    return Upstream::HostSelectionResponse{nullptr};
  }));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Invoke([host] {
    return Upstream::HostSelectionResponse{host};
  }));
  for (int i = 58; i < 100; ++i) {
    EXPECT_CALL(random_, random()).WillRepeatedly(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr).host;
    EXPECT_EQ(host.get(), target.get());
  }
}

TEST_F(AggregateClusterTest, AllHostAreUnhealthyTest) {
  initialize(default_yaml_config_);
  Upstream::HostSharedPtr host =
      Upstream::makeTestHost(primary_info_, "tcp://127.0.0.1:80", simTime());
  // Set up the HostSet with 0 healthy, 0 degraded and 2 unhealthy.
  setupPrimary(0, 0, 0, 2);
  setupPrimary(1, 0, 0, 2);

  // Set up the HostSet with 0 healthy, 0 degraded and 2 unhealthy.
  setupSecondary(0, 0, 0, 2);
  setupSecondary(1, 0, 0, 2);
  // Health value:
  // Cluster 1:
  //     Priority 0: 0%
  //     Priority 1: 0%
  // Cluster 2:
  //     Priority 0: 0%
  //     Priority 1: 0%
  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Invoke([host] {
    return Upstream::HostSelectionResponse{host};
  }));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Invoke([] {
    return Upstream::HostSelectionResponse{nullptr};
  }));

  // Choose the first cluster as the second one is unavailable.
  for (int i = 0; i < 50; ++i) {
    EXPECT_CALL(random_, random()).WillRepeatedly(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr).host;
    EXPECT_EQ(host.get(), target.get());
  }

  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Invoke([] {
    return Upstream::HostSelectionResponse{nullptr};
  }));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Invoke([host] {
    return Upstream::HostSelectionResponse{host};
  }));

  // Choose the second cluster as the first one is unavailable.
  for (int i = 50; i < 100; ++i) {
    EXPECT_CALL(random_, random()).WillRepeatedly(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr).host;
    EXPECT_EQ(host.get(), target.get());
  }
}

TEST_F(AggregateClusterTest, ClusterInPanicTest) {
  initialize(default_yaml_config_);
  Upstream::HostSharedPtr host =
      Upstream::makeTestHost(primary_info_, "tcp://127.0.0.1:80", simTime());
  setupPrimary(0, 1, 0, 4);
  setupPrimary(1, 1, 0, 4);
  setupSecondary(0, 1, 0, 4);
  setupSecondary(1, 1, 0, 4);
  // Health value:
  // Cluster 1:
  //     Priority 0: 20%
  //     Priority 1: 20%
  // Cluster 2:
  //     Priority 0: 20%
  //     Priority 1: 20%
  // All priorities are in panic mode. Traffic will be distributed evenly among four priorities.
  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Invoke([host] {
    return Upstream::HostSelectionResponse{host};
  }));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Invoke([] {
    return Upstream::HostSelectionResponse{nullptr};
  }));

  for (int i = 0; i < 50; ++i) {
    EXPECT_CALL(random_, random()).WillRepeatedly(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr).host;
    EXPECT_EQ(host.get(), target.get());
  }

  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Invoke([] {
    return Upstream::HostSelectionResponse{nullptr};
  }));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Invoke([host] {
    return Upstream::HostSelectionResponse{host};
  }));

  for (int i = 50; i < 100; ++i) {
    EXPECT_CALL(random_, random()).WillRepeatedly(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr).host;
    EXPECT_EQ(host.get(), target.get());
  }

  setupPrimary(0, 1, 0, 9);
  setupPrimary(1, 1, 0, 9);
  setupSecondary(0, 1, 0, 9);
  setupSecondary(1, 1, 0, 1);
  // Health value:
  // Cluster 1:
  //     Priority 0: 10%
  //     Priority 1: 10%
  // Cluster 2:
  //     Priority 0: 10%
  //     Priority 0: 50%
  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Invoke([host] {
    return Upstream::HostSelectionResponse{host};
  }));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Invoke([] {
    return Upstream::HostSelectionResponse{nullptr};
  }));

  for (int i = 0; i <= 25; ++i) {
    EXPECT_CALL(random_, random()).WillRepeatedly(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr).host;
    EXPECT_EQ(host.get(), target.get());
  }

  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Invoke([] {
    return Upstream::HostSelectionResponse{nullptr};
  }));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Invoke([host] {
    return Upstream::HostSelectionResponse{host};
  }));

  for (int i = 26; i < 100; ++i) {
    EXPECT_CALL(random_, random()).WillRepeatedly(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr).host;
    EXPECT_EQ(host.get(), target.get());
  }
}

TEST_F(AggregateClusterTest, LBContextTest) {
  AggregateLoadBalancerContext context(nullptr,
                                       Upstream::LoadBalancerBase::HostAvailability::Healthy, 0);

  EXPECT_EQ(context.computeHashKey().has_value(), false);
  EXPECT_EQ(context.downstreamConnection(), nullptr);
  EXPECT_EQ(context.metadataMatchCriteria(), nullptr);
  EXPECT_EQ(context.downstreamHeaders(), nullptr);
  EXPECT_EQ(context.upstreamSocketOptions(), nullptr);
  EXPECT_EQ(context.upstreamTransportSocketOptions(), nullptr);
}

TEST_F(AggregateClusterTest, ContextDeterminePriorityLoad) {
  Upstream::MockLoadBalancerContext lb_context;
  initialize(default_yaml_config_);
  setupPrimary(0, 1, 0, 0);
  setupPrimary(1, 1, 0, 0);
  setupSecondary(0, 1, 0, 0);
  setupSecondary(1, 1, 0, 0);

  const uint32_t invalid_priority = 42;
  Upstream::HostSharedPtr host =
      Upstream::makeTestHost(primary_info_, "tcp://127.0.0.1:80", simTime(), 1, invalid_priority);

  // The linearized priorities are [P0, P1, S0, S1].
  Upstream::HealthyAndDegradedLoad secondary_priority_1{Upstream::HealthyLoad({0, 0, 0, 100}),
                                                        Upstream::DegradedLoad()};

  // Validate that lb_context->determinePriorityLoad() is called and that the mapping function
  // passed in works correctly.
  EXPECT_CALL(lb_context, determinePriorityLoad(_, _, _))
      .WillOnce(Invoke([&](const Upstream::PrioritySet&, const Upstream::HealthyAndDegradedLoad&,
                           const Upstream::RetryPriority::PriorityMappingFunc& mapping_func)
                           -> const Upstream::HealthyAndDegradedLoad& {
        // This one isn't part of the mapping due to an invalid priority.
        EXPECT_FALSE(mapping_func(*host).has_value());

        // Helper to get a host from the given set and priority
        auto host_from_priority = [](Upstream::PrioritySetImpl& ps,
                                     uint32_t priority) -> const Upstream::HostDescription& {
          return *(ps.hostSetsPerPriority()[priority]->hosts()[0]);
        };

        EXPECT_EQ(mapping_func(host_from_priority(primary_ps_, 0)), absl::optional<uint32_t>(0));
        EXPECT_EQ(mapping_func(host_from_priority(primary_ps_, 1)), absl::optional<uint32_t>(1));
        EXPECT_EQ(mapping_func(host_from_priority(secondary_ps_, 0)), absl::optional<uint32_t>(2));
        EXPECT_EQ(mapping_func(host_from_priority(secondary_ps_, 1)), absl::optional<uint32_t>(3));

        return secondary_priority_1;
      }));

  // Validate that the AggregateLoadBalancerContext is initialized with the weights from
  // lb_context->determinePriorityLoad().
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_))
      .WillOnce(Invoke([this, &host](
                           Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
        const Upstream::HealthyAndDegradedLoad& adjusted_load = context->determinePriorityLoad(
            secondary_ps_, {Upstream::HealthyLoad({100, 0}), Upstream::DegradedLoad()}, nullptr);

        EXPECT_EQ(adjusted_load.healthy_priority_load_.get().size(), 2);
        EXPECT_EQ(adjusted_load.healthy_priority_load_.get().at(0), 0);
        EXPECT_EQ(adjusted_load.healthy_priority_load_.get().at(1), 100);

        return host;
      }));

  lb_->chooseHost(&lb_context);
}

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
