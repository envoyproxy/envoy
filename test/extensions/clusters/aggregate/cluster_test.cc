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

  // NOTE: in order to use remaining_<name> (and for it to not always be 0)
  // we need to use "track_remaining: true" in the config to get the remaining_<name> stat
  Stats::Gauge& getCircuitBreakersStatByPriority(std::string priority, std::string stat) {
    std::string stat_name_ = "circuit_breakers." + priority + "." + stat;
    Stats::StatNameManagedStorage statStore(stat_name_,
                                            cluster_->info()->statsScope().symbolTable());
    return cluster_->info()->statsScope().gaugeFromStatName(statStore.statName(),
                                                            Stats::Gauge::ImportMode::Accumulate);
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

  // resource manager for the DEFAULT priority
  Upstream::ResourceManager& resource_manager =
      cluster_->info()->resourceManager(Upstream::ResourcePriority::Default);

  // the default circuit breaker values are:
  // max_connections : 1024
  // max_pending_requests : 1024
  // max_requests : 1024
  // max_retries : 3

  EXPECT_EQ(1024U, resource_manager.connections().max());
  for (int i = 0; i < 1024; ++i) {
    resource_manager.connections().inc();
  }
  EXPECT_EQ(1024U, resource_manager.connections().count());
  EXPECT_FALSE(resource_manager.connections().canCreate());

  EXPECT_EQ(1024U, resource_manager.pendingRequests().max());
  for (int i = 0; i < 1024; ++i) {
    resource_manager.pendingRequests().inc();
  }
  EXPECT_EQ(1024U, resource_manager.pendingRequests().count());
  EXPECT_FALSE(resource_manager.pendingRequests().canCreate());

  EXPECT_EQ(1024U, resource_manager.requests().max());
  for (int i = 0; i < 1024; ++i) {
    resource_manager.requests().inc();
  }
  EXPECT_EQ(1024U, resource_manager.requests().count());
  EXPECT_FALSE(resource_manager.requests().canCreate());

  EXPECT_EQ(3U, resource_manager.retries().max());
  for (int i = 0; i < 3; ++i) {
    resource_manager.retries().inc();
  }
  EXPECT_EQ(3U, resource_manager.retries().count());
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

  // resource manager for the DEFAULT priority (look above^)
  Upstream::ResourceManager& resource_manager =
      cluster_->info()->resourceManager(Upstream::ResourcePriority::Default);

  // get the circuit breaker stats we are interested in, to assert against
  Stats::Gauge& cx_open = getCircuitBreakersStatByPriority("default", "cx_open");
  Stats::Gauge& remaining_cx = getCircuitBreakersStatByPriority("default", "remaining_cx");

  // check the yaml config is set correctly
  // we should have a maximum of 1 connection available to use
  EXPECT_EQ(1U, resource_manager.connections().max());

  // check that we can create a new connection
  EXPECT_TRUE(resource_manager.connections().canCreate());
  // check the connection count is 0
  EXPECT_EQ(0U, resource_manager.connections().count());
  // check that we have 1 remaining connection
  EXPECT_EQ(1U, remaining_cx.value());
  // check the circuit breaker is closed
  EXPECT_EQ(0U, cx_open.value());

  // create that one connection
  resource_manager.connections().inc();

  // check the connection count is now 1
  EXPECT_EQ(1U, resource_manager.connections().count());
  // make sure we are NOT allowed to create anymore connections
  EXPECT_FALSE(resource_manager.connections().canCreate());
  // check that we have 0 remaining connections
  EXPECT_EQ(0U, remaining_cx.value());
  // check the circuit breaker is now open
  EXPECT_EQ(1U, cx_open.value());

  // remove that one connection
  resource_manager.connections().dec();

  // check the connection count is now 0 again
  EXPECT_EQ(0U, resource_manager.connections().count());
  // check that we can create a new connection again
  EXPECT_TRUE(resource_manager.connections().canCreate());
  // check that we have 1 remaining connection again
  EXPECT_EQ(1U, remaining_cx.value());
  // check that the circuit breaker is closed again
  EXPECT_EQ(0U, cx_open.value());
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

  // resource manager for the DEFAULT priority (look above^)
  Upstream::ResourceManager& resource_manager =
      cluster_->info()->resourceManager(Upstream::ResourcePriority::Default);

  // get the circuit breaker stats we are interested in, to assert against
  Stats::Gauge& rq_pending_open = getCircuitBreakersStatByPriority("default", "rq_pending_open");
  Stats::Gauge& remaining_pending =
      getCircuitBreakersStatByPriority("default", "remaining_pending");

  // check the yaml config is set correctly
  // we should have a maximum of 1 pending request
  EXPECT_EQ(1U, resource_manager.pendingRequests().max());

  // check that we can create a new pending request
  EXPECT_TRUE(resource_manager.pendingRequests().canCreate());
  // check the pending requests count is 0
  EXPECT_EQ(0U, resource_manager.pendingRequests().count());
  // check that we have 1 remaining pending request
  EXPECT_EQ(1U, remaining_pending.value());
  // check the circuit breaker is closed
  EXPECT_EQ(0U, rq_pending_open.value());

  // create that one pending request
  resource_manager.pendingRequests().inc();

  // check the pending requests count is now 1
  EXPECT_EQ(1U, resource_manager.pendingRequests().count());
  // make sure we are NOT allowed to create anymore pending requests
  EXPECT_FALSE(resource_manager.pendingRequests().canCreate());
  // check that we have 0 remaining pending requests
  EXPECT_EQ(0U, remaining_pending.value());
  // check the circuit breaker is now open
  EXPECT_EQ(1U, rq_pending_open.value());

  // remove that one pending request
  resource_manager.pendingRequests().dec();

  // check the pending requests count is now 0 again
  EXPECT_EQ(0U, resource_manager.pendingRequests().count());
  // check that we can create a new pending request again
  EXPECT_TRUE(resource_manager.pendingRequests().canCreate());
  // check that we have 1 remaining pending request again
  EXPECT_EQ(1U, remaining_pending.value());
  // check that the circuit breaker is closed again
  EXPECT_EQ(0U, rq_pending_open.value());
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

  // resource manager for the DEFAULT priority (look above^)
  Upstream::ResourceManager& resource_manager =
      cluster_->info()->resourceManager(Upstream::ResourcePriority::Default);

  Stats::Gauge& rq_open = getCircuitBreakersStatByPriority("default", "rq_open");
  Stats::Gauge& remaining_rq = getCircuitBreakersStatByPriority("default", "remaining_rq");

  // check the yaml config is set correctly
  // we should have a maximum of 1 request available to use
  EXPECT_EQ(1U, resource_manager.requests().max());

  // check that we can create a new request
  EXPECT_TRUE(resource_manager.requests().canCreate());
  // check the requests count is 0
  EXPECT_EQ(0U, resource_manager.requests().count());
  // check that we have 1 remaining request
  EXPECT_EQ(1U, remaining_rq.value());
  // check the circuit breaker is closed
  EXPECT_EQ(0U, rq_open.value());

  // create that one request
  resource_manager.requests().inc();

  // check the request count is now 1
  EXPECT_EQ(1U, resource_manager.requests().count());
  // make sure we are NOT allowed to create anymore request
  EXPECT_FALSE(resource_manager.requests().canCreate());
  // check that we have 0 remaining requests
  EXPECT_EQ(0U, remaining_rq.value());
  // check the circuit breaker is now open
  EXPECT_EQ(1U, rq_open.value());

  // remove that one request
  resource_manager.requests().dec();

  // check the request count is now 0 again
  EXPECT_EQ(0U, resource_manager.requests().count());
  // check that we can create a new request again
  EXPECT_TRUE(resource_manager.requests().canCreate());
  // check that we have 1 remaining request again
  EXPECT_EQ(1U, remaining_rq.value());
  // check that the circuit breaker is closed again
  EXPECT_EQ(0U, rq_open.value());
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

  // resource manager for the DEFAULT priority (look above^)
  Upstream::ResourceManager& resource_manager =
      cluster_->info()->resourceManager(Upstream::ResourcePriority::Default);

  Stats::Gauge& rq_retry_open = getCircuitBreakersStatByPriority("default", "rq_retry_open");
  Stats::Gauge& remaining_retries =
      getCircuitBreakersStatByPriority("default", "remaining_retries");

  // check the yaml config is set correctly
  // we should have a maximum of 1 retry available to use
  EXPECT_EQ(1U, resource_manager.retries().max());
  // check that we can retry
  EXPECT_TRUE(resource_manager.retries().canCreate());
  // check the retries count is 0
  EXPECT_EQ(0U, resource_manager.retries().count());
  // check that we have 1 remaining retry
  EXPECT_EQ(1U, remaining_retries.value());
  // check the circuit breaker is closed
  EXPECT_EQ(0U, rq_retry_open.value());

  resource_manager.retries().inc();

  // check the retries count is now 1
  EXPECT_EQ(1U, resource_manager.retries().count());
  // make sure we are NOT allowed to create anymore retries
  EXPECT_FALSE(resource_manager.retries().canCreate());
  // check that we have 0 remaining retries
  EXPECT_EQ(0U, remaining_retries.value());
  // check the circuit breaker is now open
  EXPECT_EQ(1U, rq_retry_open.value());

  // remove that one retry
  resource_manager.retries().dec();

  // check the retries count is now 0 again
  EXPECT_EQ(0U, resource_manager.retries().count());
  // check that we can create a new retry again
  EXPECT_TRUE(resource_manager.retries().canCreate());
  // check that we have 1 remaining retry again
  EXPECT_EQ(1U, remaining_retries.value());
  // check that the circuit breaker is closed again
  EXPECT_EQ(0U, rq_retry_open.value());
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

  // resource manager for the DEFAULT priority (look above^)
  Upstream::ResourceManager& resource_manager =
      cluster_->info()->resourceManager(Upstream::ResourcePriority::Default);

  Stats::Gauge& cx_pool_open = getCircuitBreakersStatByPriority("default", "cx_pool_open");
  Stats::Gauge& remaining_cx_pools =
      getCircuitBreakersStatByPriority("default", "remaining_cx_pools");

  // check the yaml config is set correctly
  // we should have a maximum of 1 request available to use
  EXPECT_EQ(1U, resource_manager.connectionPools().max());

  // check that we can create a new connection pool
  EXPECT_TRUE(resource_manager.connectionPools().canCreate());
  // check the connection pool count is 0
  EXPECT_EQ(0U, resource_manager.connectionPools().count());
  // check that we have 1 remaining connection pool
  EXPECT_EQ(1U, remaining_cx_pools.value());
  // check the circuit breaker is closed
  EXPECT_EQ(0U, cx_pool_open.value());

  // create that one request
  resource_manager.connectionPools().inc();

  // check the connection pool count is now 1
  EXPECT_EQ(1U, resource_manager.connectionPools().count());
  // make sure we are NOT allowed to create anymore connection pools
  EXPECT_FALSE(resource_manager.connectionPools().canCreate());
  // check that we have 0 remaining connection pools
  EXPECT_EQ(0U, remaining_cx_pools.value());
  // check the circuit breaker is now open
  EXPECT_EQ(1U, cx_pool_open.value());

  // remove that one request
  resource_manager.connectionPools().dec();

  // check the connection pool count is now 0 again
  EXPECT_EQ(0U, resource_manager.connectionPools().count());
  // check that we can create a new connection pool again
  EXPECT_TRUE(resource_manager.connectionPools().canCreate());
  // check that we have 1 remaining connection pool again
  EXPECT_EQ(1U, remaining_cx_pools.value());
  // check that the circuit breaker is closed again
  EXPECT_EQ(0U, cx_pool_open.value());
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
