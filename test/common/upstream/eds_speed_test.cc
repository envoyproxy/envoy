// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "common/config/utility.h"
#include "common/singleton/manager_impl.h"
#include "common/upstream/eds.h"

#include "server/transport_socket_config_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Upstream {

class EdsSpeedTest {
public:
  EdsSpeedTest() : api_(Api::createApiForTest(stats_)) { resetCluster(); }

  void resetCluster() {
    resetCluster(R"EOF(
      name: name
      connect_timeout: 0.25s
      type: EDS
      lb_policy: ROUND_ROBIN
      eds_cluster_config:
        service_name: fare
        eds_config:
          api_config_source:
            api_type: REST
            cluster_names:
            - eds
            refresh_delay: 1s
    )EOF",
                 Cluster::InitializePhase::Secondary);
  }

  void resetClusterDrainOnHostRemoval() {
    resetCluster(R"EOF(
        name: name
        connect_timeout: 0.25s
        type: EDS
        lb_policy: ROUND_ROBIN
        drain_connections_on_host_removal: true
        eds_cluster_config:
          service_name: fare
          eds_config:
            api_config_source:
              api_type: REST
              cluster_names:
              - eds
              refresh_delay: 1s
    )EOF",
                 Cluster::InitializePhase::Secondary);
  }

  void resetClusterLoadedFromFile() {
    resetCluster(R"EOF(
      name: name
      connect_timeout: 0.25s
      type: EDS
      lb_policy: ROUND_ROBIN
      eds_cluster_config:
        eds_config:
          path: "eds path"
    )EOF",
                 Cluster::InitializePhase::Primary);
  }

  void resetCluster(const std::string& yaml_config, Cluster::InitializePhase initialize_phase) {
    local_info_.node_.mutable_locality()->set_zone("us-east-1a");
    eds_cluster_ = parseClusterFromV2Yaml(yaml_config);
    Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
        "cluster.{}.",
        eds_cluster_.alt_stat_name().empty() ? eds_cluster_.name() : eds_cluster_.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
        singleton_manager_, tls_, validation_visitor_, *api_);
    cluster_.reset(
        new EdsClusterImpl(eds_cluster_, runtime_, factory_context, std::move(scope), false));
    EXPECT_EQ(initialize_phase, cluster_->initializePhase());
    eds_callbacks_ = cm_.subscription_factory_.callbacks_;
  }

  void initialize() {
    EXPECT_CALL(*cm_.subscription_factory_.subscription_, start(_));
    cluster_->initialize([this] { initialized_ = true; });
  }

  void doOnConfigUpdateVerifyNoThrow(
      const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment) {
    Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
    resources.Add()->PackFrom(cluster_load_assignment);
    VERBOSE_EXPECT_NO_THROW(eds_callbacks_->onConfigUpdate(resources, ""));
  }

  // Set up an EDS config with multiple priorities, localities, weights and make sure
  // they are loaded and reloaded as expected.
  void PriorityAndLocalityWeightedHelper(bool ignore_unknown_dynamic_fields, int num_hosts) {
    envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
    cluster_load_assignment.set_cluster_name("fare");
    resetCluster(R"EOF(
      name: name
      connect_timeout: 0.25s
      type: EDS
      lb_policy: ROUND_ROBIN
      common_lb_config:
        locality_weighted_lb_config: {}
      eds_cluster_config:
        service_name: fare
        eds_config:
          api_config_source:
            api_type: REST
            cluster_names:
            - eds
            refresh_delay: 1s
    )EOF",
                 Envoy::Upstream::Cluster::InitializePhase::Secondary);

    uint32_t port = 1000;
    auto add_hosts_to_locality_and_priority =
        [&cluster_load_assignment, &port](const std::string& region, const std::string& zone,
                                          const std::string& sub_zone, uint32_t priority,
                                          uint32_t n, uint32_t weight) {
          auto* endpoints = cluster_load_assignment.add_endpoints();
          endpoints->set_priority(priority);
          auto* locality = endpoints->mutable_locality();
          locality->set_region(region);
          locality->set_zone(zone);
          locality->set_sub_zone(sub_zone);
          endpoints->mutable_load_balancing_weight()->set_value(weight);

          for (uint32_t i = 0; i < n; ++i) {
            auto* socket_address = endpoints->add_lb_endpoints()
<<<<<<< HEAD
                                       ->mutable_endpoint()
                                       ->mutable_address()
                                       ->mutable_socket_address();
=======
                                   ->mutable_endpoint()
                                   ->mutable_address()
                                   ->mutable_socket_address();
>>>>>>> 00437463ff... Create a benchmark test for eds updates.

            socket_address->set_address("10.0.1." + std::to_string(i / 60000));
            socket_address->set_port_value((port + i) % 60000);
          }
        };

    // Set up both priority 0 and priority 1 with 2 localities.
    add_hosts_to_locality_and_priority("oceania", "koala", "ingsoc", 0, num_hosts, 25);
    // add_hosts_to_locality_and_priority("oceania", "koala", "ingsoc", 0, 2, 25);
    add_hosts_to_locality_and_priority("", "us-east-1a", "", 0, 1, 75);
    add_hosts_to_locality_and_priority("", "us-east-1a", "", 1, 8, 60);
    add_hosts_to_locality_and_priority("foo", "bar", "eep", 1, 2, 40);
    validation_visitor_.setSkipValidation(ignore_unknown_dynamic_fields);
    initialize();
    doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
    ASSERT(initialized_);
  }

  bool initialized_{};
  Stats::IsolatedStoreImpl stats_;
  Ssl::MockContextManager ssl_context_manager_;
  envoy::config::cluster::v3::Cluster eds_cluster_;
  NiceMock<MockClusterManager> cm_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<EdsClusterImpl> cluster_;
  Config::SubscriptionCallbacks* eds_callbacks_{};
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Server::MockAdmin> admin_;
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  NiceMock<ThreadLocal::MockInstance> tls_;
  ProtobufMessage::ValidationVisitor& validation_visitor_ = ProtobufMessage::getStrictValidationVisitor();;
  Api::ApiPtr api_;
};

} // namespace Upstream
} // namespace Envoy

static void BM_PriorityAndLocalityWeighted(benchmark::State& state) {
  Envoy::Thread::MutexBasicLockable lock;
  Envoy::Logger::Context logging_state(spdlog::level::warn,
                                       Envoy::Logger::Logger::DEFAULT_LOG_FORMAT, lock, false);
  for (auto _ : state) {
    Envoy::Upstream::EdsSpeedTest speed_test;
    speed_test.PriorityAndLocalityWeightedHelper(state.range(0), state.range(1));
  }
}

BENCHMARK(BM_PriorityAndLocalityWeighted)->Ranges({{false, true}, {2000, 100000}});
