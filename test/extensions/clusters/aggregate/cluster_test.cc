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
    Config::Utility::translateOpaqueConfig(cluster_config.cluster_type().typed_config(),
                                           ProtobufMessage::getStrictValidationVisitor(), config);

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

    ON_CALL(primary_, loadBalancer()).WillByDefault(ReturnRef(primary_load_balancer_));
    ON_CALL(secondary_, loadBalancer()).WillByDefault(ReturnRef(secondary_load_balancer_));

    thread_aware_lb_ = std::make_unique<AggregateThreadAwareLoadBalancer>(*cluster_);
    lb_factory_ = thread_aware_lb_->factory();
    lb_ = lb_factory_->create(lb_params_);
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
  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(host));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(nullptr));

  for (int i = 0; i <= 65; ++i) {
    EXPECT_CALL(random_, random()).WillRepeatedly(Return(i));
    EXPECT_TRUE(lb_->peekAnotherHost(nullptr) == nullptr);
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr);
    OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetime_callbacks =
        lb_->lifetimeCallbacks();
    EXPECT_FALSE(lifetime_callbacks.has_value());
    std::vector<uint8_t> hash_key = {1, 2, 3};
    absl::optional<Upstream::SelectedPoolAndConnection> selection =
        lb_->selectExistingConnection(nullptr, *host, hash_key);
    EXPECT_FALSE(selection.has_value());
    EXPECT_EQ(host.get(), target.get());
  }

  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(host));
  for (int i = 66; i < 100; ++i) {
    EXPECT_CALL(random_, random()).WillRepeatedly(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr);
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
  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(host));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(nullptr));

  for (int i = 0; i <= 57; ++i) {
    EXPECT_CALL(random_, random()).WillRepeatedly(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr);
    EXPECT_EQ(host.get(), target.get());
  }

  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(host));
  for (int i = 58; i < 100; ++i) {
    EXPECT_CALL(random_, random()).WillRepeatedly(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr);
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
  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(host));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(nullptr));

  // Choose the first cluster as the second one is unavailable.
  for (int i = 0; i < 50; ++i) {
    EXPECT_CALL(random_, random()).WillRepeatedly(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr);
    EXPECT_EQ(host.get(), target.get());
  }

  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(host));

  // Choose the second cluster as the first one is unavailable.
  for (int i = 50; i < 100; ++i) {
    EXPECT_CALL(random_, random()).WillRepeatedly(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr);
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
  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(host));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(nullptr));

  for (int i = 0; i < 50; ++i) {
    EXPECT_CALL(random_, random()).WillRepeatedly(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr);
    EXPECT_EQ(host.get(), target.get());
  }

  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(host));

  for (int i = 50; i < 100; ++i) {
    EXPECT_CALL(random_, random()).WillRepeatedly(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr);
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
  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(host));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(nullptr));

  for (int i = 0; i <= 25; ++i) {
    EXPECT_CALL(random_, random()).WillRepeatedly(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr);
    EXPECT_EQ(host.get(), target.get());
  }

  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(host));

  for (int i = 26; i < 100; ++i) {
    EXPECT_CALL(random_, random()).WillRepeatedly(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr);
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
