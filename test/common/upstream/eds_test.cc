#include <memory>

#include "envoy/api/v2/eds.pb.h"
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

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Upstream {
namespace {

class EdsTest : public testing::Test {
protected:
  EdsTest() : api_(Api::createApiForTest(stats_)) { resetCluster(); }

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
      const envoy::api::v2::ClusterLoadAssignment& cluster_load_assignment) {
    Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
    resources.Add()->PackFrom(cluster_load_assignment);
    VERBOSE_EXPECT_NO_THROW(eds_callbacks_->onConfigUpdate(resources, ""));
  }

  bool initialized_{};
  Stats::IsolatedStoreImpl stats_;
  Ssl::MockContextManager ssl_context_manager_;
  envoy::api::v2::Cluster eds_cluster_;
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
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_;
};

class EdsWithHealthCheckUpdateTest : public EdsTest {
protected:
  EdsWithHealthCheckUpdateTest() = default;

  // Build the initial cluster with some endpoints.
  void initializeCluster(const std::vector<uint32_t> endpoint_ports,
                         const bool drain_connections_on_host_removal) {
    resetCluster(drain_connections_on_host_removal);

    auto health_checker = std::make_shared<MockHealthChecker>();
    EXPECT_CALL(*health_checker, start());
    EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_)).Times(2);
    cluster_->setHealthChecker(health_checker);

    cluster_load_assignment_.set_cluster_name("fare");

    for (const auto& port : endpoint_ports) {
      addEndpoint(port);
    }

    doOnConfigUpdateVerifyNoThrow(cluster_load_assignment_);

    // Make sure the cluster is rebuilt.
    EXPECT_EQ(0UL, stats_.counter("cluster.name.update_no_rebuild").value());
    {
      auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
      EXPECT_EQ(hosts.size(), 2);

      EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
      EXPECT_TRUE(hosts[1]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));

      // Remove the pending HC flag. This is normally done by the health checker.
      hosts[0]->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);
      hosts[1]->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);

      // Mark the hosts as healthy
      hosts[0]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
      hosts[1]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
    }
  }

  void resetCluster(const bool drain_connections_on_host_removal) {
    const std::string config = R"EOF(
      name: name
      connect_timeout: 0.25s
      type: EDS
      lb_policy: ROUND_ROBIN
      drain_connections_on_host_removal: {}
      eds_cluster_config:
        service_name: fare
        eds_config:
          api_config_source:
            api_type: REST
            cluster_names:
            - eds
            refresh_delay: 1s
      )EOF";
    EdsTest::resetCluster(fmt::format(config, drain_connections_on_host_removal),
                          Cluster::InitializePhase::Secondary);
  }

  void addEndpoint(const uint32_t port) {
    auto* endpoints = cluster_load_assignment_.add_endpoints();
    auto* socket_address = endpoints->add_lb_endpoints()
                               ->mutable_endpoint()
                               ->mutable_address()
                               ->mutable_socket_address();
    socket_address->set_address("1.2.3.4");
    socket_address->set_port_value(port);
  }

  void updateEndpointHealthCheckPortAtIndex(const uint32_t index, const uint32_t port) {
    cluster_load_assignment_.mutable_endpoints(index)
        ->mutable_lb_endpoints(0)
        ->mutable_endpoint()
        ->mutable_health_check_config()
        ->set_port_value(port);

    doOnConfigUpdateVerifyNoThrow(cluster_load_assignment_);

    // Always rebuild if health check config is changed.
    EXPECT_EQ(0UL, stats_.counter("cluster.name.update_no_rebuild").value());
  }

  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment_;
};

// Negative test for protoc-gen-validate constraints.
TEST_F(EdsTest, ValidateFail) {
  initialize();
  envoy::api::v2::ClusterLoadAssignment resource;
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
  resources.Add()->PackFrom(resource);
  EXPECT_THROW(eds_callbacks_->onConfigUpdate(resources, ""), ProtoValidationException);
  EXPECT_FALSE(initialized_);
}

// Validate that onConfigUpdate() with unexpected cluster names rejects config.
TEST_F(EdsTest, OnConfigUpdateWrongName) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("wrong name");
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
  resources.Add()->PackFrom(cluster_load_assignment);
  initialize();
  try {
    eds_callbacks_->onConfigUpdate(resources, "");
  } catch (const EnvoyException& e) {
    eds_callbacks_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected,
                                         &e);
  }
  EXPECT_TRUE(initialized_);
}

// Validate that onConfigUpdate() with empty cluster vector size ignores config.
TEST_F(EdsTest, OnConfigUpdateEmpty) {
  initialize();
  eds_callbacks_->onConfigUpdate({}, "");
  Protobuf::RepeatedPtrField<envoy::api::v2::Resource> resources;
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  eds_callbacks_->onConfigUpdate(resources, removed_resources, "");
  EXPECT_EQ(2UL, stats_.counter("cluster.name.update_empty").value());
  EXPECT_TRUE(initialized_);
}

// Validate that onConfigUpdate() with unexpected cluster vector size rejects config.
TEST_F(EdsTest, OnConfigUpdateWrongSize) {
  initialize();
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
  resources.Add()->PackFrom(cluster_load_assignment);
  resources.Add()->PackFrom(cluster_load_assignment);
  try {
    eds_callbacks_->onConfigUpdate(resources, "");
  } catch (const EnvoyException& e) {
    eds_callbacks_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected,
                                         &e);
  }
  EXPECT_TRUE(initialized_);
}

// Validate that onConfigUpdate() with the expected cluster accepts config.
TEST_F(EdsTest, OnConfigUpdateSuccess) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(initialized_);
  EXPECT_EQ(1UL, stats_.counter("cluster.name.update_no_rebuild").value());
}

// Validate that delta-style onConfigUpdate() with the expected cluster accepts config.
TEST_F(EdsTest, DeltaOnConfigUpdateSuccess) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  initialize();

  Protobuf::RepeatedPtrField<envoy::api::v2::Resource> resources;
  auto* resource = resources.Add();
  resource->mutable_resource()->PackFrom(cluster_load_assignment);
  resource->set_version("v1");
  VERBOSE_EXPECT_NO_THROW(eds_callbacks_->onConfigUpdate(resources, {}, "v1"));

  EXPECT_TRUE(initialized_);
  EXPECT_EQ(1UL, stats_.counter("cluster.name.update_no_rebuild").value());
}

// Validate that onConfigUpdate() with no service name accepts config.
TEST_F(EdsTest, NoServiceNameOnSuccessConfigUpdate) {
  resetCluster(R"EOF(
      name: name
      connect_timeout: 0.25s
      type: EDS
      lb_policy: ROUND_ROBIN
      eds_cluster_config:
        eds_config:
          api_config_source:
            api_type: REST
            cluster_names:
            - eds
            refresh_delay: 1s
    )EOF",
               Cluster::InitializePhase::Secondary);
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("name");
  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(initialized_);
}

// Validate that EDS cluster loaded from file as primary cluster
TEST_F(EdsTest, EdsClusterFromFileIsPrimaryCluster) {
  resetClusterLoadedFromFile();
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("name");
  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(initialized_);
}

// Validate that onConfigUpdate() updates the endpoint metadata.
TEST_F(EdsTest, EndpointMetadata) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  auto* endpoints = cluster_load_assignment.add_endpoints();
  auto* endpoint = endpoints->add_lb_endpoints();
  auto* canary = endpoints->add_lb_endpoints();

  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("1.2.3.4");
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);
  Config::Metadata::mutableMetadataValue(*endpoint->mutable_metadata(),
                                         Config::MetadataFilters::get().ENVOY_LB, "string_key")
      .set_string_value("string_value");
  Config::Metadata::mutableMetadataValue(*endpoint->mutable_metadata(), "custom_namespace",
                                         "num_key")
      .set_number_value(1.1);

  canary->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("2.3.4.5");
  canary->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);
  Config::Metadata::mutableMetadataValue(*canary->mutable_metadata(),
                                         Config::MetadataFilters::get().ENVOY_LB,
                                         Config::MetadataEnvoyLbKeys::get().CANARY)
      .set_bool_value(true);
  Config::Metadata::mutableMetadataValue(*canary->mutable_metadata(),
                                         Config::MetadataFilters::get().ENVOY_LB, "version")
      .set_string_value("v1");

  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(initialized_);
  EXPECT_EQ(0UL, stats_.counter("cluster.name.update_no_rebuild").value());

  auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(hosts.size(), 2);
  EXPECT_EQ(hosts[0]->metadata()->filter_metadata_size(), 2);
  EXPECT_EQ(Config::Metadata::metadataValue(*hosts[0]->metadata(),
                                            Config::MetadataFilters::get().ENVOY_LB, "string_key")
                .string_value(),
            std::string("string_value"));
  EXPECT_EQ(Config::Metadata::metadataValue(*hosts[0]->metadata(), "custom_namespace", "num_key")
                .number_value(),
            1.1);
  EXPECT_FALSE(Config::Metadata::metadataValue(*hosts[0]->metadata(),
                                               Config::MetadataFilters::get().ENVOY_LB,
                                               Config::MetadataEnvoyLbKeys::get().CANARY)
                   .bool_value());
  EXPECT_FALSE(hosts[0]->canary());

  EXPECT_EQ(hosts[1]->metadata()->filter_metadata_size(), 1);
  EXPECT_TRUE(Config::Metadata::metadataValue(*hosts[1]->metadata(),
                                              Config::MetadataFilters::get().ENVOY_LB,
                                              Config::MetadataEnvoyLbKeys::get().CANARY)
                  .bool_value());
  EXPECT_TRUE(hosts[1]->canary());
  EXPECT_EQ(Config::Metadata::metadataValue(*hosts[1]->metadata(),
                                            Config::MetadataFilters::get().ENVOY_LB, "version")
                .string_value(),
            "v1");

  // We don't rebuild with the exact same config.
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_EQ(1UL, stats_.counter("cluster.name.update_no_rebuild").value());

  // New resources with Metadata updated.
  Config::Metadata::mutableMetadataValue(*canary->mutable_metadata(),
                                         Config::MetadataFilters::get().ENVOY_LB, "version")
      .set_string_value("v2");
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  auto& nhosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(nhosts.size(), 2);
  EXPECT_EQ(Config::Metadata::metadataValue(*nhosts[1]->metadata(),
                                            Config::MetadataFilters::get().ENVOY_LB, "version")
                .string_value(),
            "v2");
}

// Validate that onConfigUpdate() updates endpoint health status.
TEST_F(EdsTest, EndpointHealthStatus) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  auto* endpoints = cluster_load_assignment.add_endpoints();

  // First check that EDS is correctly mapping
  // envoy::api::v2::core::HealthStatus values to the expected health() status.
  const std::vector<std::pair<envoy::api::v2::core::HealthStatus, Host::Health>>
      health_status_expected = {
          {envoy::api::v2::core::HealthStatus::UNKNOWN, Host::Health::Healthy},
          {envoy::api::v2::core::HealthStatus::HEALTHY, Host::Health::Healthy},
          {envoy::api::v2::core::HealthStatus::UNHEALTHY, Host::Health::Unhealthy},
          {envoy::api::v2::core::HealthStatus::DRAINING, Host::Health::Unhealthy},
          {envoy::api::v2::core::HealthStatus::TIMEOUT, Host::Health::Unhealthy},
          {envoy::api::v2::core::HealthStatus::DEGRADED, Host::Health::Degraded},
      };

  int port = 80;
  for (auto hs : health_status_expected) {
    auto* endpoint = endpoints->add_lb_endpoints();
    auto* socket_address =
        endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address();
    socket_address->set_address("1.2.3.4");
    socket_address->set_port_value(port++);
    endpoint->set_health_status(hs.first);
  }

  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(initialized_);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), health_status_expected.size());

    for (uint32_t i = 0; i < hosts.size(); ++i) {
      EXPECT_EQ(health_status_expected[i].second, hosts[i]->health());
    }
  }

  // Perform an update in which we don't change the host set, but flip some host
  // to unhealthy, check we have the expected change in status.
  endpoints->mutable_lb_endpoints(0)->set_health_status(
      envoy::api::v2::core::HealthStatus::UNHEALTHY);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), health_status_expected.size());
    EXPECT_EQ(Host::Health::Unhealthy, hosts[0]->health());

    for (uint32_t i = 1; i < hosts.size(); ++i) {
      EXPECT_EQ(health_status_expected[i].second, hosts[i]->health());
    }
  }

  // Perform an update in which we don't change the host set, but flip some host
  // to healthy, check we have the expected change in status.
  endpoints->mutable_lb_endpoints(health_status_expected.size() - 1)
      ->set_health_status(envoy::api::v2::core::HealthStatus::HEALTHY);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), health_status_expected.size());
    EXPECT_EQ(Host::Health::Healthy, hosts[hosts.size() - 1]->health());

    for (uint32_t i = 1; i < hosts.size() - 1; ++i) {
      EXPECT_EQ(health_status_expected[i].second, hosts[i]->health());
    }
  }

  // Mark host 0 unhealthy from active health checking as well, we should have
  // the same situation as above.
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    hosts[0]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  }
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(Host::Health::Unhealthy, hosts[0]->health());
  }

  // Now mark host 0 healthy via EDS, it should still be unhealthy due to the
  // active health check failure.
  endpoints->mutable_lb_endpoints(0)->set_health_status(
      envoy::api::v2::core::HealthStatus::HEALTHY);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(Host::Health::Unhealthy, hosts[0]->health());
  }

  // Finally, mark host 0 healthy again via active health check. It should be
  // immediately healthy again.
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    hosts[0]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
    EXPECT_EQ(Host::Health::Healthy, hosts[0]->health());
  }

  const auto rebuild_container = stats_.counter("cluster.name.update_no_rebuild").value();
  // Now mark host 0 degraded via EDS, it should be degraded.
  endpoints->mutable_lb_endpoints(0)->set_health_status(
      envoy::api::v2::core::HealthStatus::DEGRADED);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(Host::Health::Degraded, hosts[0]->health());
  }

  // We should rebuild the cluster since we went from healthy -> degraded.
  EXPECT_EQ(rebuild_container, stats_.counter("cluster.name.update_no_rebuild").value());

  // Now mark the host as having been degraded through active hc.
  cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthFlagSet(
      Host::HealthFlag::DEGRADED_ACTIVE_HC);

  // Now mark host 0 healthy via EDS, it should still be degraded.
  endpoints->mutable_lb_endpoints(0)->set_health_status(
      envoy::api::v2::core::HealthStatus::HEALTHY);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(Host::Health::Degraded, hosts[0]->health());
  }

  // Since the host health didn't change, expect no rebuild.
  EXPECT_EQ(rebuild_container + 1, stats_.counter("cluster.name.update_no_rebuild").value());
}

// Verify that a host is removed if it is removed from discovery, stabilized, and then later
// fails active HC.
TEST_F(EdsTest, EndpointRemovalAfterHcFail) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");

  auto health_checker = std::make_shared<MockHealthChecker>();
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_)).Times(2);
  cluster_->setHealthChecker(health_checker);

  auto add_endpoint = [&cluster_load_assignment](int port) {
    auto* endpoints = cluster_load_assignment.add_endpoints();

    auto* socket_address = endpoints->add_lb_endpoints()
                               ->mutable_endpoint()
                               ->mutable_address()
                               ->mutable_socket_address();
    socket_address->set_address("1.2.3.4");
    socket_address->set_port_value(port);
  };

  add_endpoint(80);
  add_endpoint(81);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 2);

    // Remove the pending HC flag. This is normally done by the health checker.
    hosts[0]->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);
    hosts[1]->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);

    // Mark the hosts as healthy
    hosts[0]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
    hosts[1]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  }

  // Remove endpoints and add back the port 80 one. Both hosts should be present due to
  // being stabilized, but one of them should be marked pending removal.
  cluster_load_assignment.clear_endpoints();
  add_endpoint(80);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 2);
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
    EXPECT_TRUE(hosts[1]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
  }

  // Add both hosts back, make sure pending removal is gone.
  cluster_load_assignment.clear_endpoints();
  add_endpoint(80);
  add_endpoint(81);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 2);
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
    EXPECT_FALSE(hosts[1]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
  }

  // Remove endpoints and add back the port 80 one. Both hosts should be present due to
  // being stabilized, but one of them should be marked pending removal.
  cluster_load_assignment.clear_endpoints();
  add_endpoint(80);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  HostSharedPtr not_removed_host;
  HostSharedPtr removed_host;
  {
    EXPECT_EQ(2,
              cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get()[0].size());
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 2);
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
    EXPECT_TRUE(hosts[1]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));

    // Mark the host is failing active HC and then run callbacks.
    not_removed_host = hosts[0];
    removed_host = hosts[1];
    hosts[1]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
    health_checker->runCallbacks(hosts[1], HealthTransition::Changed);
  }

  {
    EXPECT_EQ(1,
              cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get()[0].size());
    EXPECT_EQ(1, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  }

  // Add back 81. Verify that we have a new host. This will show that the all_hosts_ was updated
  // correctly.
  cluster_load_assignment.clear_endpoints();
  add_endpoint(80);
  add_endpoint(81);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 2);
    EXPECT_EQ(not_removed_host, hosts[0]);
    EXPECT_EQ(removed_host->address()->asString(), hosts[1]->address()->asString());
    EXPECT_NE(removed_host, hosts[1]);
  }
}

// Verify that a host is removed when it is still passing active HC, but has been previously
// told by the EDS server to fail health check.
TEST_F(EdsTest, EndpointRemovalEdsFailButActiveHcSuccess) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  auto* endpoints = cluster_load_assignment.add_endpoints();

  auto health_checker = std::make_shared<MockHealthChecker>();
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_)).Times(2);
  cluster_->setHealthChecker(health_checker);

  auto add_endpoint = [endpoints](int port) {
    auto* socket_address = endpoints->add_lb_endpoints()
                               ->mutable_endpoint()
                               ->mutable_address()
                               ->mutable_socket_address();
    socket_address->set_address("1.2.3.4");
    socket_address->set_port_value(port);
  };

  add_endpoint(80);
  add_endpoint(81);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 2);

    // Remove the pending HC flag. This is normally done by the health checker.
    hosts[0]->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);
    hosts[1]->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);

    // Mark the hosts as healthy
    hosts[0]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
    hosts[1]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  }

  // Mark the first endpoint as unhealthy from EDS.
  endpoints->mutable_lb_endpoints(0)->set_health_status(
      envoy::api::v2::core::HealthStatus::UNHEALTHY);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 2);

    EXPECT_EQ(hosts[0]->health(), Host::Health::Unhealthy);
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_EDS_HEALTH));
    EXPECT_EQ(hosts[1]->health(), Host::Health::Healthy);
  }

  // Now remove the first host. Even though it is still passing active HC, since EDS has
  // previously explicitly failed it, we won't stabilize it anymore.
  endpoints->mutable_lb_endpoints()->erase(endpoints->mutable_lb_endpoints()->begin());
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);
  }
}

// Validate that onConfigUpdate() removes endpoints that are marked as healthy
// when configured to drain on host removal.
TEST_F(EdsTest, EndpointRemovalClusterDrainOnHostRemoval) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  resetClusterDrainOnHostRemoval();

  auto health_checker = std::make_shared<MockHealthChecker>();
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_)).Times(2);
  cluster_->setHealthChecker(health_checker);

  auto add_endpoint = [&cluster_load_assignment](int port) {
    auto* endpoints = cluster_load_assignment.add_endpoints();

    auto* socket_address = endpoints->add_lb_endpoints()
                               ->mutable_endpoint()
                               ->mutable_address()
                               ->mutable_socket_address();
    socket_address->set_address("1.2.3.4");
    socket_address->set_port_value(port);
  };

  add_endpoint(80);
  add_endpoint(81);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 2);

    EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_TRUE(hosts[1]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));

    // Remove the pending HC flag. This is normally done by the health checker.
    hosts[0]->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);
    hosts[1]->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);

    // Mark the hosts as healthy
    hosts[0]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
    hosts[1]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  }

  // Remove endpoints and add back the port 80 one
  cluster_load_assignment.clear_endpoints();
  add_endpoint(80);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);
  }
}

// Verifies that if an endpoint is moved to a new priority, the active hc status is preserved.
TEST_F(EdsTest, EndpointMovedToNewPriority) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  resetClusterDrainOnHostRemoval();

  auto health_checker = std::make_shared<MockHealthChecker>();
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_)).Times(2);
  cluster_->setHealthChecker(health_checker);

  auto add_endpoint = [&cluster_load_assignment](int port, int priority) {
    auto* endpoints = cluster_load_assignment.add_endpoints();
    endpoints->set_priority(priority);

    auto* socket_address = endpoints->add_lb_endpoints()
                               ->mutable_endpoint()
                               ->mutable_address()
                               ->mutable_socket_address();
    socket_address->set_address("1.2.3.4");
    socket_address->set_port_value(port);
  };

  add_endpoint(80, 0);
  add_endpoint(81, 0);

  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 2);

    // Mark the hosts as healthy
    for (auto& host : hosts) {
      EXPECT_TRUE(host->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
      host->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
      host->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);
    }
  }

  // Moves the endpoints between priorities
  cluster_load_assignment.clear_endpoints();
  add_endpoint(81, 0);
  add_endpoint(80, 1);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);

    // assert that it didn't move
    EXPECT_EQ(hosts[0]->address()->asString(), "1.2.3.4:81");

    // The endpoint was healthy in the original priority, so moving it
    // around should preserve that.
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  }

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[1]->hosts();
    EXPECT_EQ(hosts.size(), 1);

    // assert that it moved
    EXPECT_EQ(hosts[0]->address()->asString(), "1.2.3.4:80");

    // The endpoint was healthy in the original priority, so moving it
    // around should preserve that.
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  }
}

// Verifies that if an endpoint is moved between priorities, the health check value
// of the host is preserved
TEST_F(EdsTest, EndpointMoved) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  resetClusterDrainOnHostRemoval();

  auto health_checker = std::make_shared<MockHealthChecker>();
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_)).Times(2);
  cluster_->setHealthChecker(health_checker);

  auto add_endpoint = [&cluster_load_assignment](int port, int priority) {
    auto* endpoints = cluster_load_assignment.add_endpoints();
    endpoints->set_priority(priority);

    auto* socket_address = endpoints->add_lb_endpoints()
                               ->mutable_endpoint()
                               ->mutable_address()
                               ->mutable_socket_address();
    socket_address->set_address("1.2.3.4");
    socket_address->set_port_value(port);
  };

  add_endpoint(80, 0);
  add_endpoint(81, 1);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);

    EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_EQ(0, hosts[0]->priority());
    // Mark the host as healthy and remove the pending active hc flag.
    hosts[0]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
    hosts[0]->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);
  }

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[1]->hosts();
    EXPECT_EQ(hosts.size(), 1);

    EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_EQ(1, hosts[0]->priority());
    // Mark the host as healthy and remove the pending active hc flag.
    hosts[0]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
    hosts[0]->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);
  }

  // Moves the endpoints between priorities
  cluster_load_assignment.clear_endpoints();
  add_endpoint(81, 0);
  add_endpoint(80, 1);
  // Verify that no hosts gets added or removed to/from the PrioritySet.
  cluster_->prioritySet().addMemberUpdateCb([&](const auto& added, const auto& removed) {
    EXPECT_TRUE(added.empty());
    EXPECT_TRUE(removed.empty());
  });
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);

    // assert that it moved
    EXPECT_EQ(hosts[0]->address()->asString(), "1.2.3.4:81");
    EXPECT_EQ(0, hosts[0]->priority());

    // The endpoint was healthy in the original priority, so moving it
    // around should preserve that.
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  }

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[1]->hosts();
    EXPECT_EQ(hosts.size(), 1);

    // assert that it moved
    EXPECT_EQ(hosts[0]->address()->asString(), "1.2.3.4:80");
    EXPECT_EQ(1, hosts[0]->priority());

    // The endpoint was healthy in the original priority, so moving it
    // around should preserve that.
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  }
}

// Validates that we correctly update the host list when a new overprovisioning factor is set.
TEST_F(EdsTest, EndpointAddedWithNewOverprovisioningFactor) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  cluster_load_assignment.mutable_policy()->mutable_overprovisioning_factor()->set_value(1000);

  {
    auto* endpoints = cluster_load_assignment.add_endpoints();
    auto* locality = endpoints->mutable_locality();
    locality->set_region("oceania");
    locality->set_zone("hello");
    locality->set_sub_zone("world");
    endpoints->mutable_load_balancing_weight()->set_value(42);

    auto* endpoint_address = endpoints->add_lb_endpoints()
                                 ->mutable_endpoint()
                                 ->mutable_address()
                                 ->mutable_socket_address();
    endpoint_address->set_address("1.2.3.4");
    endpoint_address->set_port_value(80);
  }

  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(initialized_);

  EXPECT_EQ(1, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ("1.2.3.4:80",
            cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->address()->asString());
}

// Validate that onConfigUpdate() updates the endpoint locality.
TEST_F(EdsTest, EndpointLocality) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  auto* endpoints = cluster_load_assignment.add_endpoints();
  auto* locality = endpoints->mutable_locality();
  locality->set_region("oceania");
  locality->set_zone("hello");
  locality->set_sub_zone("world");

  {
    auto* endpoint_address = endpoints->add_lb_endpoints()
                                 ->mutable_endpoint()
                                 ->mutable_address()
                                 ->mutable_socket_address();
    endpoint_address->set_address("1.2.3.4");
    endpoint_address->set_port_value(80);
  }
  {
    auto* endpoint_address = endpoints->add_lb_endpoints()
                                 ->mutable_endpoint()
                                 ->mutable_address()
                                 ->mutable_socket_address();
    endpoint_address->set_address("2.3.4.5");
    endpoint_address->set_port_value(80);
  }

  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(initialized_);

  auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(hosts.size(), 2);
  for (int i = 0; i < 2; ++i) {
    EXPECT_EQ(0, hosts[i]->priority());
    const auto& locality = hosts[i]->locality();
    EXPECT_EQ("oceania", locality.region());
    EXPECT_EQ("hello", locality.zone());
    EXPECT_EQ("world", locality.sub_zone());
  }
  EXPECT_EQ(nullptr, cluster_->prioritySet().hostSetsPerPriority()[0]->localityWeights());
}

// Validate that onConfigUpdate() does not propagate locality weights to the host set when
// locality weighted balancing isn't configured.
TEST_F(EdsTest, EndpointLocalityWeightsIgnored) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");

  {
    auto* endpoints = cluster_load_assignment.add_endpoints();
    auto* locality = endpoints->mutable_locality();
    locality->set_region("oceania");
    locality->set_zone("hello");
    locality->set_sub_zone("world");
    endpoints->mutable_load_balancing_weight()->set_value(42);

    auto* endpoint_address = endpoints->add_lb_endpoints()
                                 ->mutable_endpoint()
                                 ->mutable_address()
                                 ->mutable_socket_address();
    endpoint_address->set_address("1.2.3.4");
    endpoint_address->set_port_value(80);
  }

  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(initialized_);

  EXPECT_EQ(nullptr, cluster_->prioritySet().hostSetsPerPriority()[0]->localityWeights());
}

// Validate that onConfigUpdate() propagates locality weights to the host set when locality
// weighted balancing is configured.
TEST_F(EdsTest, EndpointLocalityWeights) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
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
               Cluster::InitializePhase::Secondary);

  {
    auto* endpoints = cluster_load_assignment.add_endpoints();
    auto* locality = endpoints->mutable_locality();
    locality->set_region("oceania");
    locality->set_zone("hello");
    locality->set_sub_zone("world");
    endpoints->mutable_load_balancing_weight()->set_value(42);

    auto* endpoint_address = endpoints->add_lb_endpoints()
                                 ->mutable_endpoint()
                                 ->mutable_address()
                                 ->mutable_socket_address();
    endpoint_address->set_address("1.2.3.4");
    endpoint_address->set_port_value(80);
  }

  {
    auto* endpoints = cluster_load_assignment.add_endpoints();
    auto* locality = endpoints->mutable_locality();
    locality->set_region("space");
    locality->set_zone("station");
    locality->set_sub_zone("international");

    auto* endpoint_address = endpoints->add_lb_endpoints()
                                 ->mutable_endpoint()
                                 ->mutable_address()
                                 ->mutable_socket_address();
    endpoint_address->set_address("1.2.3.5");
    endpoint_address->set_port_value(80);
  }

  {
    auto* endpoints = cluster_load_assignment.add_endpoints();
    auto* locality = endpoints->mutable_locality();
    locality->set_region("sugar");
    locality->set_zone("candy");
    locality->set_sub_zone("mountain");
    endpoints->mutable_load_balancing_weight()->set_value(37);

    auto* endpoint_address = endpoints->add_lb_endpoints()
                                 ->mutable_endpoint()
                                 ->mutable_address()
                                 ->mutable_socket_address();
    endpoint_address->set_address("1.2.3.6");
    endpoint_address->set_port_value(80);
  }

  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(initialized_);

  const auto& locality_weights =
      *cluster_->prioritySet().hostSetsPerPriority()[0]->localityWeights();
  EXPECT_EQ(3, locality_weights.size());
  EXPECT_EQ(42, locality_weights[0]);
  EXPECT_EQ(0, locality_weights[1]);
  EXPECT_EQ(37, locality_weights[2]);
}

// Validate that onConfigUpdate() removes any locality not referenced in the
// config update in each priority.
TEST_F(EdsTest, RemoveUnreferencedLocalities) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  uint32_t port = 1000;
  auto add_hosts_to_locality = [&cluster_load_assignment,
                                &port](const std::string& region, const std::string& zone,
                                       const std::string& sub_zone, uint32_t n, uint32_t priority) {
    auto* endpoints = cluster_load_assignment.add_endpoints();
    endpoints->set_priority(priority);
    auto* locality = endpoints->mutable_locality();
    locality->set_region(region);
    locality->set_zone(zone);
    locality->set_sub_zone(sub_zone);

    for (uint32_t i = 0; i < n; ++i) {
      auto* socket_address = endpoints->add_lb_endpoints()
                                 ->mutable_endpoint()
                                 ->mutable_address()
                                 ->mutable_socket_address();
      socket_address->set_address("1.2.3.4");
      socket_address->set_port_value(port++);
    }
  };

  // Add two localities to each of priority 0 and 1
  add_hosts_to_locality("oceania", "koala", "ingsoc", 2, 0);
  add_hosts_to_locality("", "us-east-1a", "", 1, 0);

  add_hosts_to_locality("oceania", "bear", "best", 4, 1);
  add_hosts_to_locality("", "us-west-1a", "", 2, 1);

  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(initialized_);

  {
    auto& hosts_per_locality =
        cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get();
    EXPECT_EQ(2, hosts_per_locality.size());
  }

  {
    auto& hosts_per_locality =
        cluster_->prioritySet().hostSetsPerPriority()[1]->hostsPerLocality().get();
    EXPECT_EQ(2, hosts_per_locality.size());
  }

  // Reset the ClusterLoadAssignment to only contain one of the locality per priority.
  // This should leave us with only one locality.
  cluster_load_assignment.clear_endpoints();
  add_hosts_to_locality("oceania", "koala", "ingsoc", 4, 0);
  add_hosts_to_locality("oceania", "bear", "best", 2, 1);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts_per_locality =
        cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get();
    EXPECT_EQ(1, hosts_per_locality.size());
  }

  {
    auto& hosts_per_locality =
        cluster_->prioritySet().hostSetsPerPriority()[1]->hostsPerLocality().get();
    EXPECT_EQ(1, hosts_per_locality.size());
  }

  // Clear out the new ClusterLoadAssignment. This should leave us with 0 localities per priority.
  cluster_load_assignment.clear_endpoints();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts_per_locality =
        cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get();
    EXPECT_EQ(0, hosts_per_locality.size());
  }

  {
    auto& hosts_per_locality =
        cluster_->prioritySet().hostSetsPerPriority()[1]->hostsPerLocality().get();
    EXPECT_EQ(0, hosts_per_locality.size());
  }
}

// Validate that onConfigUpdate() updates bins hosts per locality as expected.
TEST_F(EdsTest, EndpointHostsPerLocality) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  uint32_t port = 1000;
  auto add_hosts_to_locality = [&cluster_load_assignment,
                                &port](const std::string& region, const std::string& zone,
                                       const std::string& sub_zone, uint32_t n) {
    auto* endpoints = cluster_load_assignment.add_endpoints();
    auto* locality = endpoints->mutable_locality();
    locality->set_region(region);
    locality->set_zone(zone);
    locality->set_sub_zone(sub_zone);

    for (uint32_t i = 0; i < n; ++i) {
      auto* socket_address = endpoints->add_lb_endpoints()
                                 ->mutable_endpoint()
                                 ->mutable_address()
                                 ->mutable_socket_address();
      socket_address->set_address("1.2.3.4");
      socket_address->set_port_value(port++);
    }
  };

  add_hosts_to_locality("oceania", "koala", "ingsoc", 2);
  add_hosts_to_locality("", "us-east-1a", "", 1);

  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(initialized_);

  {
    auto& hosts_per_locality = cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality();
    EXPECT_EQ(2, hosts_per_locality.get().size());
    EXPECT_EQ(1, hosts_per_locality.get()[0].size());
    EXPECT_THAT(Locality("", "us-east-1a", ""),
                ProtoEq(hosts_per_locality.get()[0][0]->locality()));
    EXPECT_EQ(2, hosts_per_locality.get()[1].size());
    EXPECT_THAT(Locality("oceania", "koala", "ingsoc"),
                ProtoEq(hosts_per_locality.get()[1][0]->locality()));
    EXPECT_THAT(Locality("oceania", "koala", "ingsoc"),
                ProtoEq(hosts_per_locality.get()[1][1]->locality()));
  }

  add_hosts_to_locality("oceania", "koala", "eucalyptus", 3);
  add_hosts_to_locality("general", "koala", "ingsoc", 5);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts_per_locality = cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality();
    EXPECT_EQ(4, hosts_per_locality.get().size());
    EXPECT_EQ(1, hosts_per_locality.get()[0].size());
    EXPECT_THAT(Locality("", "us-east-1a", ""),
                ProtoEq(hosts_per_locality.get()[0][0]->locality()));
    EXPECT_EQ(5, hosts_per_locality.get()[1].size());
    EXPECT_THAT(Locality("general", "koala", "ingsoc"),
                ProtoEq(hosts_per_locality.get()[1][0]->locality()));
    EXPECT_EQ(3, hosts_per_locality.get()[2].size());
    EXPECT_THAT(Locality("oceania", "koala", "eucalyptus"),
                ProtoEq(hosts_per_locality.get()[2][0]->locality()));
    EXPECT_EQ(2, hosts_per_locality.get()[3].size());
    EXPECT_THAT(Locality("oceania", "koala", "ingsoc"),
                ProtoEq(hosts_per_locality.get()[3][0]->locality()));
  }
}

// Validate that onConfigUpdate() updates all priorities in the prioritySet
TEST_F(EdsTest, EndpointHostPerPriority) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  uint32_t port = 1000;
  auto add_hosts_to_locality = [&cluster_load_assignment,
                                &port](const std::string& region, const std::string& zone,
                                       const std::string& sub_zone, uint32_t n, uint32_t priority) {
    auto* endpoints = cluster_load_assignment.add_endpoints();
    endpoints->set_priority(priority);
    auto* locality = endpoints->mutable_locality();
    locality->set_region(region);
    locality->set_zone(zone);
    locality->set_sub_zone(sub_zone);

    for (uint32_t i = 0; i < n; ++i) {
      auto* socket_address = endpoints->add_lb_endpoints()
                                 ->mutable_endpoint()
                                 ->mutable_address()
                                 ->mutable_socket_address();
      socket_address->set_address("1.2.3.4");
      socket_address->set_port_value(port++);
    }
  };

  add_hosts_to_locality("oceania", "koala", "ingsoc", 2, 0);
  add_hosts_to_locality("", "us-east-1a", "", 1, 1);

  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(initialized_);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(2, hosts.size());
  }

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[1]->hosts();
    EXPECT_EQ(1, hosts.size());
  }

  cluster_load_assignment.clear_endpoints();

  add_hosts_to_locality("oceania", "koala", "ingsoc", 4, 0);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(4, hosts.size());
  }

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[1]->hosts();
    EXPECT_EQ(0, hosts.size());
  }
}

// Validate that onConfigUpdate() updates bins hosts per priority as expected.
TEST_F(EdsTest, EndpointHostsPerPriority) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  uint32_t port = 1000;
  auto add_hosts_to_priority = [&cluster_load_assignment, &port](uint32_t priority, uint32_t n) {
    auto* endpoints = cluster_load_assignment.add_endpoints();
    endpoints->set_priority(priority);

    for (uint32_t i = 0; i < n; ++i) {
      auto* socket_address = endpoints->add_lb_endpoints()
                                 ->mutable_endpoint()
                                 ->mutable_address()
                                 ->mutable_socket_address();
      socket_address->set_address("1.2.3.4");
      socket_address->set_port_value(port++);
    }
  };

  // Set up the priority levels so 0 has two hosts and 1 has one host.
  add_hosts_to_priority(0, 2);
  add_hosts_to_priority(1, 1);

  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(initialized_);

  ASSERT_EQ(2, cluster_->prioritySet().hostSetsPerPriority().size());
  EXPECT_EQ(2, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(1, cluster_->prioritySet().hostSetsPerPriority()[1]->hosts().size());

  // Add 2 more hosts to priority 0, and add five hosts to priority 2.
  // Note the (illegal) gap (no priority 1.)  Until we have config validation,
  // make sure bad config does no harm.
  add_hosts_to_priority(0, 2);
  add_hosts_to_priority(3, 5);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  ASSERT_EQ(4, cluster_->prioritySet().hostSetsPerPriority().size());
  EXPECT_EQ(4, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(1, cluster_->prioritySet().hostSetsPerPriority()[1]->hosts().size());
  EXPECT_EQ(0, cluster_->prioritySet().hostSetsPerPriority()[2]->hosts().size());
  EXPECT_EQ(5, cluster_->prioritySet().hostSetsPerPriority()[3]->hosts().size());

  // Update the number of hosts in priority #4. Make sure no other priority
  // levels are affected.
  cluster_load_assignment.clear_endpoints();
  add_hosts_to_priority(3, 4);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  ASSERT_EQ(4, cluster_->prioritySet().hostSetsPerPriority().size());
  EXPECT_EQ(4, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(1, cluster_->prioritySet().hostSetsPerPriority()[1]->hosts().size());
  EXPECT_EQ(0, cluster_->prioritySet().hostSetsPerPriority()[2]->hosts().size());
  EXPECT_EQ(4, cluster_->prioritySet().hostSetsPerPriority()[3]->hosts().size());
}

// Make sure config updates with P!=0 are rejected for the local cluster.
TEST_F(EdsTest, NoPriorityForLocalCluster) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  cm_.local_cluster_name_ = "fare";
  uint32_t port = 1000;
  auto add_hosts_to_priority = [&cluster_load_assignment, &port](uint32_t priority, uint32_t n) {
    auto* endpoints = cluster_load_assignment.add_endpoints();
    endpoints->set_priority(priority);

    for (uint32_t i = 0; i < n; ++i) {
      auto* socket_address = endpoints->add_lb_endpoints()
                                 ->mutable_endpoint()
                                 ->mutable_address()
                                 ->mutable_socket_address();
      socket_address->set_address("1.2.3.4");
      socket_address->set_port_value(port++);
    }
  };

  // Set up the priority levels so 0 has two hosts and 1 has one host. Update
  // should fail.
  add_hosts_to_priority(0, 2);
  add_hosts_to_priority(1, 1);
  initialize();
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
  resources.Add()->PackFrom(cluster_load_assignment);
  EXPECT_THROW_WITH_MESSAGE(eds_callbacks_->onConfigUpdate(resources, ""), EnvoyException,
                            "Unexpected non-zero priority for local cluster 'fare'.");

  // Try an update which only has endpoints with P=0. This should go through.
  cluster_load_assignment.clear_endpoints();
  add_hosts_to_priority(0, 2);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
}

// Set up an EDS config with multiple priorities and localities and make sure
// they are loaded and reloaded as expected.
TEST_F(EdsTest, PriorityAndLocality) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  uint32_t port = 1000;
  auto add_hosts_to_locality_and_priority =
      [&cluster_load_assignment, &port](const std::string& region, const std::string& zone,
                                        const std::string& sub_zone, uint32_t priority,
                                        uint32_t n) {
        auto* endpoints = cluster_load_assignment.add_endpoints();
        endpoints->set_priority(priority);
        auto* locality = endpoints->mutable_locality();
        locality->set_region(region);
        locality->set_zone(zone);
        locality->set_sub_zone(sub_zone);

        for (uint32_t i = 0; i < n; ++i) {
          auto* socket_address = endpoints->add_lb_endpoints()
                                     ->mutable_endpoint()
                                     ->mutable_address()
                                     ->mutable_socket_address();
          socket_address->set_address("1.2.3.4");
          socket_address->set_port_value(port++);
        }
      };

  // Set up both priority 0 and priority 1 with 2 localities.
  add_hosts_to_locality_and_priority("oceania", "koala", "ingsoc", 0, 2);
  add_hosts_to_locality_and_priority("", "us-east-1a", "", 0, 1);
  add_hosts_to_locality_and_priority("", "us-east-1a", "", 1, 8);
  add_hosts_to_locality_and_priority("foo", "bar", "eep", 1, 2);

  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(initialized_);

  {
    auto& first_hosts_per_locality =
        cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality();
    EXPECT_EQ(2, first_hosts_per_locality.get().size());
    EXPECT_EQ(1, first_hosts_per_locality.get()[0].size());
    EXPECT_THAT(Locality("", "us-east-1a", ""),
                ProtoEq(first_hosts_per_locality.get()[0][0]->locality()));
    EXPECT_EQ(2, first_hosts_per_locality.get()[1].size());
    EXPECT_THAT(Locality("oceania", "koala", "ingsoc"),
                ProtoEq(first_hosts_per_locality.get()[1][0]->locality()));
    EXPECT_THAT(Locality("oceania", "koala", "ingsoc"),
                ProtoEq(first_hosts_per_locality.get()[1][1]->locality()));

    auto& second_hosts_per_locality =
        cluster_->prioritySet().hostSetsPerPriority()[1]->hostsPerLocality();
    ASSERT_EQ(2, second_hosts_per_locality.get().size());
    EXPECT_EQ(8, second_hosts_per_locality.get()[0].size());
    EXPECT_EQ(2, second_hosts_per_locality.get()[1].size());
  }

  // Add one more locality to both priority 0 and priority 1.
  add_hosts_to_locality_and_priority("oceania", "koala", "eucalyptus", 0, 3);
  add_hosts_to_locality_and_priority("general", "koala", "ingsoc", 1, 5);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& first_hosts_per_locality =
        cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality();
    EXPECT_EQ(3, first_hosts_per_locality.get().size());
    EXPECT_EQ(1, first_hosts_per_locality.get()[0].size());
    EXPECT_THAT(Locality("", "us-east-1a", ""),
                ProtoEq(first_hosts_per_locality.get()[0][0]->locality()));
    EXPECT_EQ(3, first_hosts_per_locality.get()[1].size());
    EXPECT_THAT(Locality("oceania", "koala", "eucalyptus"),
                ProtoEq(first_hosts_per_locality.get()[1][0]->locality()));
    EXPECT_EQ(2, first_hosts_per_locality.get()[2].size());
    EXPECT_THAT(Locality("oceania", "koala", "ingsoc"),
                ProtoEq(first_hosts_per_locality.get()[2][0]->locality()));

    auto& second_hosts_per_locality =
        cluster_->prioritySet().hostSetsPerPriority()[1]->hostsPerLocality();
    EXPECT_EQ(3, second_hosts_per_locality.get().size());
    EXPECT_EQ(8, second_hosts_per_locality.get()[0].size());
    EXPECT_THAT(Locality("", "us-east-1a", ""),
                ProtoEq(second_hosts_per_locality.get()[0][0]->locality()));
    EXPECT_EQ(2, second_hosts_per_locality.get()[1].size());
    EXPECT_THAT(Locality("foo", "bar", "eep"),
                ProtoEq(second_hosts_per_locality.get()[1][0]->locality()));
    EXPECT_EQ(5, second_hosts_per_locality.get()[2].size());
    EXPECT_THAT(Locality("general", "koala", "ingsoc"),
                ProtoEq(second_hosts_per_locality.get()[2][0]->locality()));
  }
}

// Set up an EDS config with multiple priorities, localities, weights and make sure
// they are loaded and reloaded as expected.
TEST_F(EdsTest, PriorityAndLocalityWeighted) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
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
               Cluster::InitializePhase::Secondary);

  uint32_t port = 1000;
  auto add_hosts_to_locality_and_priority =
      [&cluster_load_assignment, &port](const std::string& region, const std::string& zone,
                                        const std::string& sub_zone, uint32_t priority, uint32_t n,
                                        uint32_t weight) {
        auto* endpoints = cluster_load_assignment.add_endpoints();
        endpoints->set_priority(priority);
        auto* locality = endpoints->mutable_locality();
        locality->set_region(region);
        locality->set_zone(zone);
        locality->set_sub_zone(sub_zone);
        endpoints->mutable_load_balancing_weight()->set_value(weight);

        for (uint32_t i = 0; i < n; ++i) {
          auto* socket_address = endpoints->add_lb_endpoints()
                                     ->mutable_endpoint()
                                     ->mutable_address()
                                     ->mutable_socket_address();
          socket_address->set_address("1.2.3.4");
          socket_address->set_port_value(port++);
        }
      };

  // Set up both priority 0 and priority 1 with 2 localities.
  add_hosts_to_locality_and_priority("oceania", "koala", "ingsoc", 0, 2, 25);
  add_hosts_to_locality_and_priority("", "us-east-1a", "", 0, 1, 75);
  add_hosts_to_locality_and_priority("", "us-east-1a", "", 1, 8, 60);
  add_hosts_to_locality_and_priority("foo", "bar", "eep", 1, 2, 40);

  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(initialized_);
  EXPECT_EQ(0UL, stats_.counter("cluster.name.update_no_rebuild").value());

  {
    auto& first_hosts_per_locality =
        cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality();
    auto& first_locality_weights =
        *cluster_->prioritySet().hostSetsPerPriority()[0]->localityWeights();
    EXPECT_EQ(2, first_hosts_per_locality.get().size());
    EXPECT_EQ(1, first_hosts_per_locality.get()[0].size());
    EXPECT_THAT(Locality("", "us-east-1a", ""),
                ProtoEq(first_hosts_per_locality.get()[0][0]->locality()));
    EXPECT_EQ(75, first_locality_weights[0]);
    EXPECT_EQ(2, first_hosts_per_locality.get()[1].size());
    EXPECT_THAT(Locality("oceania", "koala", "ingsoc"),
                ProtoEq(first_hosts_per_locality.get()[1][0]->locality()));
    EXPECT_THAT(Locality("oceania", "koala", "ingsoc"),
                ProtoEq(first_hosts_per_locality.get()[1][1]->locality()));
    EXPECT_EQ(25, first_locality_weights[1]);

    auto& second_hosts_per_locality =
        cluster_->prioritySet().hostSetsPerPriority()[1]->hostsPerLocality();
    auto& second_locality_weights =
        *cluster_->prioritySet().hostSetsPerPriority()[1]->localityWeights();
    ASSERT_EQ(2, second_hosts_per_locality.get().size());
    EXPECT_EQ(8, second_hosts_per_locality.get()[0].size());
    EXPECT_EQ(60, second_locality_weights[0]);
    EXPECT_EQ(2, second_hosts_per_locality.get()[1].size());
    EXPECT_EQ(40, second_locality_weights[1]);
  }

  // This should noop (regression test for earlier bug where we would still
  // rebuild).
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_EQ(1UL, stats_.counter("cluster.name.update_no_rebuild").value());

  // Adjust locality weights, validate that we observe an update.
  cluster_load_assignment.mutable_endpoints(0)->mutable_load_balancing_weight()->set_value(60);
  cluster_load_assignment.mutable_endpoints(1)->mutable_load_balancing_weight()->set_value(40);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_EQ(1UL, stats_.counter("cluster.name.update_no_rebuild").value());
}

TEST_F(EdsWithHealthCheckUpdateTest, EndpointUpdateHealthCheckConfig) {
  const std::vector<uint32_t> endpoint_ports = {80, 81};
  const uint32_t new_health_check_port = 8000;

  // Initialize the cluster with two endpoints without draining connections on host removal.
  initializeCluster(endpoint_ports, false);

  updateEndpointHealthCheckPortAtIndex(0, new_health_check_port);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 3);
    // Make sure the first endpoint health check port is updated.
    EXPECT_EQ(new_health_check_port, hosts[0]->healthCheckAddress()->ip()->port());

    EXPECT_NE(new_health_check_port, hosts[1]->healthCheckAddress()->ip()->port());
    EXPECT_NE(new_health_check_port, hosts[2]->healthCheckAddress()->ip()->port());
    EXPECT_EQ(endpoint_ports[1], hosts[1]->healthCheckAddress()->ip()->port());
    EXPECT_EQ(endpoint_ports[0], hosts[2]->healthCheckAddress()->ip()->port());

    EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));

    // The old hosts are still active. The health checker continues to do health checking to these
    // hosts, until they are removed.
    EXPECT_FALSE(hosts[1]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_FALSE(hosts[2]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  }

  updateEndpointHealthCheckPortAtIndex(1, new_health_check_port);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 4);
    EXPECT_EQ(new_health_check_port, hosts[0]->healthCheckAddress()->ip()->port());

    // Make sure the second endpoint health check port is updated.
    EXPECT_EQ(new_health_check_port, hosts[1]->healthCheckAddress()->ip()->port());

    EXPECT_EQ(endpoint_ports[1], hosts[2]->healthCheckAddress()->ip()->port());
    EXPECT_EQ(endpoint_ports[0], hosts[3]->healthCheckAddress()->ip()->port());

    EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_TRUE(hosts[1]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));

    // The old hosts are still active.
    EXPECT_FALSE(hosts[2]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_FALSE(hosts[3]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  }
}

TEST_F(EdsWithHealthCheckUpdateTest, EndpointUpdateHealthCheckConfigWithDrainConnectionsOnRemoval) {
  const std::vector<uint32_t> endpoint_ports = {80, 81};
  const uint32_t new_health_check_port = 8000;

  // Initialize the cluster with two endpoints with draining connections on host removal.
  initializeCluster(endpoint_ports, true);

  updateEndpointHealthCheckPortAtIndex(0, new_health_check_port);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    // Since drain_connections_on_host_removal is set to true, the old hosts are removed
    // immediately.
    EXPECT_EQ(hosts.size(), 2);
    // Make sure the first endpoint health check port is updated.
    EXPECT_EQ(new_health_check_port, hosts[0]->healthCheckAddress()->ip()->port());

    EXPECT_NE(new_health_check_port, hosts[1]->healthCheckAddress()->ip()->port());
  }

  updateEndpointHealthCheckPortAtIndex(1, new_health_check_port);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 2);
    EXPECT_EQ(new_health_check_port, hosts[0]->healthCheckAddress()->ip()->port());

    // Make sure the second endpoint health check port is updated.
    EXPECT_EQ(new_health_check_port, hosts[1]->healthCheckAddress()->ip()->port());
  }
}

// Throw on adding a new resource with an invalid endpoint (since the given address is invalid).
TEST_F(EdsTest, MalformedIP) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  auto* endpoints = cluster_load_assignment.add_endpoints();

  auto* endpoint = endpoints->add_lb_endpoints();
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address(
      "foo.bar.com");
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);

  initialize();
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
  resources.Add()->PackFrom(cluster_load_assignment);
  EXPECT_THROW_WITH_MESSAGE(eds_callbacks_->onConfigUpdate(resources, ""), EnvoyException,
                            "malformed IP address: foo.bar.com. Consider setting resolver_name or "
                            "setting cluster type to 'STRICT_DNS' or 'LOGICAL_DNS'");
}

class EdsAssignmentTimeoutTest : public EdsTest {
public:
  EdsAssignmentTimeoutTest() {
    EXPECT_CALL(dispatcher_, createTimer_(_))
        .WillOnce(Invoke([this](Event::TimerCb cb) {
          timer_cb_ = cb;
          EXPECT_EQ(nullptr, interval_timer_);
          interval_timer_ = new Event::MockTimer();
          return interval_timer_;
        }))
        .WillRepeatedly(Invoke([](Event::TimerCb) { return new Event::MockTimer(); }));

    resetCluster();
  }

  Event::MockTimer* interval_timer_{nullptr};
  Event::TimerCb timer_cb_;
};

// Test that assignment timeout is enabled and disabled correctly.
TEST_F(EdsAssignmentTimeoutTest, AssignmentTimeoutEnableDisable) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  auto* endpoints = cluster_load_assignment.add_endpoints();

  auto health_checker = std::make_shared<MockHealthChecker>();
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_)).Times(2);
  cluster_->setHealthChecker(health_checker);

  auto* socket_address = endpoints->add_lb_endpoints()
                             ->mutable_endpoint()
                             ->mutable_address()
                             ->mutable_socket_address();
  socket_address->set_address("1.2.3.4");
  socket_address->set_port_value(80);

  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment_lease = cluster_load_assignment;
  cluster_load_assignment_lease.mutable_policy()->mutable_endpoint_stale_after()->MergeFrom(
      Protobuf::util::TimeUtil::SecondsToDuration(1));

  EXPECT_CALL(*interval_timer_, enableTimer(_, _)).Times(2); // Timer enabled twice.
  EXPECT_CALL(*interval_timer_, disableTimer()).Times(1);    // Timer disabled once.
  EXPECT_CALL(*interval_timer_, enabled()).Times(6);         // Includes calls by test.
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment_lease);
  // Check that the timer is enabled.
  EXPECT_EQ(interval_timer_->enabled(), true);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  // Check that the timer is disabled.
  EXPECT_EQ(interval_timer_->enabled(), false);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment_lease);
  // Check that the timer is enabled.
  EXPECT_EQ(interval_timer_->enabled(), true);
}

// Test that assignment timeout is called and removes all the endpoints.
TEST_F(EdsAssignmentTimeoutTest, AssignmentLeaseExpired) {
  envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  cluster_load_assignment.mutable_policy()->mutable_endpoint_stale_after()->MergeFrom(
      Protobuf::util::TimeUtil::SecondsToDuration(1));

  auto health_checker = std::make_shared<MockHealthChecker>();
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_)).Times(2);
  cluster_->setHealthChecker(health_checker);

  auto add_endpoint = [&cluster_load_assignment](int port) {
    auto* endpoints = cluster_load_assignment.add_endpoints();

    auto* socket_address = endpoints->add_lb_endpoints()
                               ->mutable_endpoint()
                               ->mutable_address()
                               ->mutable_socket_address();
    socket_address->set_address("1.2.3.4");
    socket_address->set_port_value(port);
  };

  // Add two endpoints to the cluster assignment.
  add_endpoint(80);
  add_endpoint(81);

  // Expect the timer to be enabled once.
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(1000), _));
  // Expect the timer to be disabled when stale assignments are removed.
  EXPECT_CALL(*interval_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enabled()).Times(2);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 2);
  }
  // Call the timer callback to indicate timeout.
  timer_cb_();
  // Test that stale endpoints are removed.
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 0);
  }
}

} // namespace
} // namespace Upstream
} // namespace Envoy
