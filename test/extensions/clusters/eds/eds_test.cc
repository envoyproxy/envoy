#include <memory>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/config/utility.h"
#include "source/common/singleton/manager_impl.h"
#include "source/extensions/clusters/eds/eds.h"
#include "source/server/transport_socket_config_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/config/eds_resources_cache.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/health_checker.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SaveArg;

namespace Envoy {
namespace Upstream {
namespace {

class EdsTest : public testing::Test, public Event::TestUsingSimulatedTime {
public:
  EdsTest() { resetCluster(); }

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

  //  Define a cluster with secure and unsecure (default) transport
  //  sockets.
  void resetClusterWithTransportSockets() {
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
      transport_socket_matches:
      - match:
          secure: enabled
        name: secure-mode
        transport_socket:
          name: envoy.transport_sockets.tls
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
      - match: {}
        name: default-mode
        transport_socket:
          name: envoy.transport_sockets.raw_buffer
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.raw_buffer.v3.RawBuffer
 )EOF",
                 Cluster::InitializePhase::Secondary);
  }

  void resetClusterDrainOnHostRemoval() {
    resetCluster(R"EOF(
        name: name
        connect_timeout: 0.25s
        type: EDS
        lb_policy: ROUND_ROBIN
        ignore_health_on_host_removal: true
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
    server_context_.local_info_.node_.mutable_locality()->set_zone("us-east-1a");
    eds_cluster_ = parseClusterFromV3Yaml(yaml_config);
    Envoy::Upstream::ClusterFactoryContextImpl factory_context(
        server_context_, server_context_.cluster_manager_, nullptr, ssl_context_manager_, nullptr,
        false);
    cluster_ = std::make_shared<EdsClusterImpl>(eds_cluster_, factory_context);
    EXPECT_EQ(initialize_phase, cluster_->initializePhase());
    eds_callbacks_ = server_context_.cluster_manager_.subscription_factory_.callbacks_;
  }

  void initialize() {
    EXPECT_CALL(server_context_, timeSource()).WillRepeatedly(testing::ReturnRef(simTime()));
    EXPECT_CALL(*server_context_.cluster_manager_.subscription_factory_.subscription_, start(_));
    cluster_->initialize([this] { initialized_ = true; });
  }

  void doOnConfigUpdateVerifyNoThrow(
      const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment) {
    const auto decoded_resources =
        TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
    EXPECT_TRUE(eds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "").ok());
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  bool initialized_{};
  Stats::TestUtil::TestStore& stats_ = server_context_.store_;
  NiceMock<Ssl::MockContextManager> ssl_context_manager_;

  envoy::config::cluster::v3::Cluster eds_cluster_;
  EdsClusterImplSharedPtr cluster_;
  Config::SubscriptionCallbacks* eds_callbacks_{};
};

class EdsWithHealthCheckUpdateTest : public EdsTest {
protected:
  EdsWithHealthCheckUpdateTest() = default;

  // Build the initial cluster with some endpoints.
  void initializeCluster(const std::vector<uint32_t> endpoint_ports,
                         const bool ignore_health_on_host_removal) {
    resetCluster(ignore_health_on_host_removal);

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
    EXPECT_EQ(0UL,
              stats_.findCounterByString("cluster.name.update_no_rebuild").value().get().value());
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

  void resetCluster(const bool ignore_health_on_host_removal) {
    constexpr absl::string_view config = R"EOF(
      name: name
      connect_timeout: 0.25s
      type: EDS
      lb_policy: ROUND_ROBIN
      ignore_health_on_host_removal: {}
      eds_cluster_config:
        service_name: fare
        eds_config:
          api_config_source:
            api_type: REST
            cluster_names:
            - eds
            refresh_delay: 1s
      )EOF";
    EdsTest::resetCluster(fmt::format(config, ignore_health_on_host_removal),
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
    EXPECT_EQ(0UL,
              stats_.findCounterByString("cluster.name.update_no_rebuild").value().get().value());
  }

  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment_;
};

// Validate that onConfigUpdate() with unexpected cluster names rejects config.
TEST_F(EdsTest, OnConfigUpdateWrongName) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("wrong name");
  const auto decoded_resources =
      TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
  initialize();
  try {
    THROW_IF_NOT_OK(eds_callbacks_->onConfigUpdate(decoded_resources.refvec_, ""));
  } catch (const EnvoyException& e) {
    eds_callbacks_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected,
                                         &e);
  }
  EXPECT_TRUE(initialized_);
}

// Validate that onConfigUpdate() with empty cluster vector size ignores config.
TEST_F(EdsTest, OnConfigUpdateEmpty) {
  initialize();
  EXPECT_TRUE(eds_callbacks_->onConfigUpdate({}, "").ok());
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_TRUE(eds_callbacks_->onConfigUpdate({}, removed_resources, "").ok());
  EXPECT_EQ(2UL, stats_.findCounterByString("cluster.name.update_empty").value().get().value());
  EXPECT_TRUE(initialized_);
}

// Validate that onConfigUpdate() with unexpected cluster vector size rejects config.
TEST_F(EdsTest, OnConfigUpdateWrongSize) {
  initialize();
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  const auto decoded_resources = TestUtility::decodeResources(
      {cluster_load_assignment, cluster_load_assignment}, "cluster_name");
  try {
    THROW_IF_NOT_OK(eds_callbacks_->onConfigUpdate(decoded_resources.refvec_, ""));
  } catch (const EnvoyException& e) {
    eds_callbacks_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected,
                                         &e);
  }
  EXPECT_TRUE(initialized_);
}

// Validate that onConfigUpdate() with the expected cluster accepts config.
TEST_F(EdsTest, OnConfigUpdateSuccess) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(initialized_);
  EXPECT_EQ(1UL,
            stats_.findCounterByString("cluster.name.update_no_rebuild").value().get().value());
}

// Validate that delta-style onConfigUpdate() with the expected cluster accepts config.
TEST_F(EdsTest, DeltaOnConfigUpdateSuccess) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  initialize();

  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> resources;
  auto* resource = resources.Add();
  resource->mutable_resource()->PackFrom(cluster_load_assignment);
  resource->set_version("v1");
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::endpoint::v3::ClusterLoadAssignment>(
          resources, "cluster_name");
  EXPECT_TRUE(eds_callbacks_->onConfigUpdate(decoded_resources.refvec_, {}, "v1").ok());

  EXPECT_TRUE(initialized_);
  EXPECT_EQ(1UL,
            stats_.findCounterByString("cluster.name.update_no_rebuild").value().get().value());
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
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("name");
  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(initialized_);
}

// Validate that EDS cluster loaded from file as primary cluster
TEST_F(EdsTest, EdsClusterFromFileIsPrimaryCluster) {
  resetClusterLoadedFromFile();
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("name");
  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(initialized_);
}

// Verify that host weight changes cause a full rebuild.
TEST_F(EdsTest, EndpointWeightChangeCausesRebuild) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  auto* endpoints = cluster_load_assignment.add_endpoints();
  auto* endpoint = endpoints->add_lb_endpoints();
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("1.2.3.4");
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);
  endpoint->mutable_load_balancing_weight()->set_value(30);

  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(initialized_);
  EXPECT_EQ(0UL,
            stats_.findCounterByString("cluster.name.update_no_rebuild").value().get().value());
  EXPECT_EQ(30UL, stats_.findGaugeByString("cluster.name.max_host_weight").value().get().value());
  auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(hosts.size(), 1);
  EXPECT_EQ(hosts[0]->weight(), 30);

  endpoint->mutable_load_balancing_weight()->set_value(31);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_EQ(0UL,
            stats_.findCounterByString("cluster.name.update_no_rebuild").value().get().value());
  EXPECT_EQ(31UL, stats_.findGaugeByString("cluster.name.max_host_weight").value().get().value());
  auto& new_hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(new_hosts.size(), 1);
  EXPECT_EQ(new_hosts[0]->weight(), 31);
}

// Verify that host weight changes cause a full rebuild.
TEST_F(EdsTest, DualStackEndpoint) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");

  // Add dual stack endpoint
  auto* endpoints = cluster_load_assignment.add_endpoints();
  auto* endpoint = endpoints->add_lb_endpoints();
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("::1");
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);
  auto* socket_address = endpoint->mutable_endpoint()
                             ->mutable_additional_addresses()
                             ->Add()
                             ->mutable_address()
                             ->mutable_socket_address();
  socket_address->set_address("1.2.3.5");
  socket_address->set_port_value(80);

  endpoint->mutable_load_balancing_weight()->set_value(30);

  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(initialized_);
  EXPECT_EQ(0UL,
            stats_.findCounterByString("cluster.name.update_no_rebuild").value().get().value());
  EXPECT_EQ(30UL, stats_.findGaugeByString("cluster.name.max_host_weight").value().get().value());
  auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(hosts.size(), 1);
  EXPECT_EQ(hosts[0]->weight(), 30);

  testing::StrictMock<Event::MockDispatcher> dispatcher;
  Network::TransportSocketOptionsConstSharedPtr transport_socket_options;
  Network::ConnectionSocket::OptionsSharedPtr options;

  auto connection = new testing::StrictMock<Network::MockClientConnection>();
  EXPECT_CALL(*connection, setBufferLimits(1048576));
  EXPECT_CALL(*connection, addConnectionCallbacks(_));
  EXPECT_CALL(*connection, connectionInfoSetter());
  // The underlying connection should be created with the first address in the list.
  EXPECT_CALL(dispatcher, createClientConnection_(hosts[0]->address(), _, _, _))
      .WillOnce(Return(connection));
  EXPECT_CALL(dispatcher, createTimer_(_));

  Envoy::Upstream::Host::CreateConnectionData connection_data =
      hosts[0]->createConnection(dispatcher, options, transport_socket_options);
  // The created connection will be wrapped in a HappyEyeballsConnectionImpl.
  EXPECT_NE(connection, connection_data.connection_.get());
}

// Validate that onConfigUpdate() updates the endpoint metadata.
TEST_F(EdsTest, EndpointMetadata) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
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
  EXPECT_EQ(0UL,
            stats_.findCounterByString("cluster.name.update_no_rebuild").value().get().value());

  auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(hosts.size(), 2);
  EXPECT_EQ(hosts[0]->metadata()->filter_metadata_size(), 2);
  EXPECT_EQ(Config::Metadata::metadataValue(hosts[0]->metadata().get(),
                                            Config::MetadataFilters::get().ENVOY_LB, "string_key")
                .string_value(),
            std::string("string_value"));
  EXPECT_EQ(
      Config::Metadata::metadataValue(hosts[0]->metadata().get(), "custom_namespace", "num_key")
          .number_value(),
      1.1);
  EXPECT_FALSE(Config::Metadata::metadataValue(hosts[0]->metadata().get(),
                                               Config::MetadataFilters::get().ENVOY_LB,
                                               Config::MetadataEnvoyLbKeys::get().CANARY)
                   .bool_value());
  EXPECT_FALSE(hosts[0]->canary());

  EXPECT_EQ(hosts[1]->metadata()->filter_metadata_size(), 1);
  EXPECT_TRUE(Config::Metadata::metadataValue(hosts[1]->metadata().get(),
                                              Config::MetadataFilters::get().ENVOY_LB,
                                              Config::MetadataEnvoyLbKeys::get().CANARY)
                  .bool_value());
  EXPECT_TRUE(hosts[1]->canary());
  EXPECT_EQ(Config::Metadata::metadataValue(hosts[1]->metadata().get(),
                                            Config::MetadataFilters::get().ENVOY_LB, "version")
                .string_value(),
            "v1");

  // We don't rebuild with the exact same config.
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_EQ(1UL,
            stats_.findCounterByString("cluster.name.update_no_rebuild").value().get().value());

  // New resources with Metadata updated.
  Config::Metadata::mutableMetadataValue(*canary->mutable_metadata(),
                                         Config::MetadataFilters::get().ENVOY_LB, "version")
      .set_string_value("v3");
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  auto& nhosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(nhosts.size(), 2);
  EXPECT_EQ(Config::Metadata::metadataValue(nhosts[1]->metadata().get(),
                                            Config::MetadataFilters::get().ENVOY_LB, "version")
                .string_value(),
            "v3");
}

// Test verifies that updating metadata updates
// data members dependent on metadata values.
// Specifically, it transport socket matcher has changed,
// the transport socket factory should also be updated.
TEST_F(EdsTest, EndpointMetadataWithTransportSocket) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  resetClusterWithTransportSockets();

  auto health_checker = std::make_shared<MockHealthChecker>();
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_)).Times(2);
  cluster_->setHealthChecker(health_checker);

  // Add single endpoint to the cluster.
  auto* endpoints = cluster_load_assignment.add_endpoints();
  auto* endpoint = endpoints->add_lb_endpoints();

  auto* socket_address = endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address();
  socket_address->set_address("1.2.3.4");
  socket_address->set_port_value(80);

  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(hosts.size(), 1);
  auto* upstream_host = hosts[0].get();

  // Verify that default transport socket is raw (does not implement secure transport).
  EXPECT_FALSE(upstream_host->transportSocketFactory().implementsSecureTransport());

  // Create metadata with transport socket match pointing to secure mode.
  auto metadata = new envoy::config::core::v3::Metadata();
  MetadataConstSharedPtr metadata_sharedptr(metadata);
  Config::Metadata::mutableMetadataValue(
      *metadata, Config::MetadataFilters::get().ENVOY_TRANSPORT_SOCKET_MATCH, "secure")
      .set_string_value("enabled");

  // Update metadata.
  upstream_host->metadata(metadata_sharedptr);

  // Transport socket factory should point to tls, which implements secure transport.
  EXPECT_TRUE(upstream_host->transportSocketFactory().implementsSecureTransport());
}

// Validate that onConfigUpdate() updates endpoint health status.
TEST_F(EdsTest, EndpointHealthStatus) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  auto* endpoints = cluster_load_assignment.add_endpoints();

  // First check that EDS is correctly mapping
  // HealthStatus values to the expected coarseHealth() status.
  const std::vector<std::pair<envoy::config::core::v3::HealthStatus, Host::Health>>
      health_status_expected = {
          {envoy::config::core::v3::UNKNOWN, Host::Health::Healthy},
          {envoy::config::core::v3::HEALTHY, Host::Health::Healthy},
          {envoy::config::core::v3::UNHEALTHY, Host::Health::Unhealthy},
          {envoy::config::core::v3::DRAINING, Host::Health::Unhealthy},
          {envoy::config::core::v3::TIMEOUT, Host::Health::Unhealthy},
          {envoy::config::core::v3::DEGRADED, Host::Health::Degraded},
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
      EXPECT_EQ(health_status_expected[i].second, hosts[i]->coarseHealth());
      EXPECT_EQ(health_status_expected[i].first, hosts[i]->edsHealthStatus());
    }
  }

  // Perform an update in which we don't change the host set, but flip some host
  // to unhealthy, check we have the expected change in status.
  endpoints->mutable_lb_endpoints(0)->set_health_status(envoy::config::core::v3::UNHEALTHY);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), health_status_expected.size());
    EXPECT_EQ(Host::Health::Unhealthy, hosts[0]->coarseHealth());
    EXPECT_EQ(envoy::config::core::v3::UNHEALTHY, hosts[0]->edsHealthStatus());

    for (uint32_t i = 1; i < hosts.size(); ++i) {
      EXPECT_EQ(health_status_expected[i].second, hosts[i]->coarseHealth());
      EXPECT_EQ(health_status_expected[i].first, hosts[i]->edsHealthStatus());
    }
  }

  // Perform an update in which we don't change the host set, but flip some host
  // to healthy, check we have the expected change in status.
  endpoints->mutable_lb_endpoints(health_status_expected.size() - 1)
      ->set_health_status(envoy::config::core::v3::HEALTHY);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), health_status_expected.size());
    EXPECT_EQ(Host::Health::Healthy, hosts[hosts.size() - 1]->coarseHealth());
    EXPECT_EQ(envoy::config::core::v3::HEALTHY, hosts[hosts.size() - 1]->edsHealthStatus());

    for (uint32_t i = 1; i < hosts.size() - 1; ++i) {
      EXPECT_EQ(health_status_expected[i].second, hosts[i]->coarseHealth());
      EXPECT_EQ(health_status_expected[i].first, hosts[i]->edsHealthStatus());
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
    EXPECT_EQ(Host::Health::Unhealthy, hosts[0]->coarseHealth());
  }

  // Now mark host 0 healthy via EDS, it should still be unhealthy due to the
  // active health check failure.
  endpoints->mutable_lb_endpoints(0)->set_health_status(envoy::config::core::v3::HEALTHY);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(Host::Health::Unhealthy, hosts[0]->coarseHealth());
  }

  // Finally, mark host 0 healthy again via active health check. It should be
  // immediately healthy again.
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    hosts[0]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
    EXPECT_EQ(Host::Health::Healthy, hosts[0]->coarseHealth());
  }

  const auto rebuild_container =
      stats_.findCounterByString("cluster.name.update_no_rebuild").value().get().value();
  // Now mark host 0 degraded via EDS, it should be degraded.
  endpoints->mutable_lb_endpoints(0)->set_health_status(envoy::config::core::v3::DEGRADED);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(Host::Health::Degraded, hosts[0]->coarseHealth());
  }

  // We should rebuild the cluster since we went from healthy -> degraded.
  EXPECT_EQ(rebuild_container,
            stats_.findCounterByString("cluster.name.update_no_rebuild").value().get().value());

  // Now mark the host as having been degraded through active hc.
  cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthFlagSet(
      Host::HealthFlag::DEGRADED_ACTIVE_HC);

  // Now mark host 0 healthy via EDS, it should still be degraded.
  endpoints->mutable_lb_endpoints(0)->set_health_status(envoy::config::core::v3::HEALTHY);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(Host::Health::Degraded, hosts[0]->coarseHealth());
  }

  // Since the host health didn't change, expect no rebuild.
  EXPECT_EQ(rebuild_container + 1,
            stats_.findCounterByString("cluster.name.update_no_rebuild").value().get().value());
}

// Validate that onConfigUpdate() updates the hostname.
TEST_F(EdsTest, Hostname) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  auto* endpoint = cluster_load_assignment.add_endpoints()->add_lb_endpoints()->mutable_endpoint();
  auto* socket_address = endpoint->mutable_address()->mutable_socket_address();
  socket_address->set_address("1.2.3.4");
  socket_address->set_port_value(1234);
  endpoint->set_hostname("foo");
  cluster_load_assignment.set_cluster_name("fare");
  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(hosts.size(), 1);
  EXPECT_EQ(hosts[0]->hostname(), "foo");
}

TEST_F(EdsTest, UseHostnameForHealthChecks) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  auto* endpoint = cluster_load_assignment.add_endpoints()->add_lb_endpoints()->mutable_endpoint();
  auto* socket_address = endpoint->mutable_address()->mutable_socket_address();
  socket_address->set_address("1.2.3.4");
  socket_address->set_port_value(1234);
  endpoint->mutable_health_check_config()->set_hostname("foo");
  cluster_load_assignment.set_cluster_name("fare");
  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(hosts.size(), 1);
  EXPECT_EQ(hosts[0]->hostnameForHealthChecks(), "foo");
}

TEST_F(EdsTest, UseAddressForHealthChecks) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  auto* endpoint = cluster_load_assignment.add_endpoints()->add_lb_endpoints()->mutable_endpoint();
  auto* socket_address = endpoint->mutable_address()->mutable_socket_address();
  socket_address->set_address("1.2.3.4");
  socket_address->set_port_value(1234);
  auto* health_check_config_address =
      endpoint->mutable_health_check_config()->mutable_address()->mutable_socket_address();
  health_check_config_address->set_address("4.3.2.1");
  health_check_config_address->set_port_value(4321);
  cluster_load_assignment.set_cluster_name("fare");
  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(hosts.size(), 1);
  EXPECT_EQ(hosts[0]->healthCheckAddress()->asString(), "4.3.2.1:4321");
}

TEST_F(EdsTest, MalformedIPForHealthChecks) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  auto* endpoint = cluster_load_assignment.add_endpoints()->add_lb_endpoints()->mutable_endpoint();
  auto* socket_address = endpoint->mutable_address()->mutable_socket_address();
  socket_address->set_address("1.2.3.4");
  socket_address->set_port_value(1234);
  auto* health_check_config_address =
      endpoint->mutable_health_check_config()->mutable_address()->mutable_socket_address();
  health_check_config_address->set_address("foo.bar.com");
  health_check_config_address->set_port_value(4321);
  cluster_load_assignment.set_cluster_name("fare");
  initialize();
  const auto decoded_resources =
      TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
  EXPECT_THROW_WITH_MESSAGE(
      EXPECT_TRUE(eds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "").ok()),
      EnvoyException, "malformed IP address: foo.bar.com");
}

// Verify that a host is removed if it is removed from discovery, stabilized, and then later
// fails active HC.
TEST_F(EdsTest, EndpointRemovalAfterHcFail) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
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
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
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
  endpoints->mutable_lb_endpoints(0)->set_health_status(envoy::config::core::v3::UNHEALTHY);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 2);

    EXPECT_EQ(hosts[0]->coarseHealth(), Host::Health::Unhealthy);
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_EDS_HEALTH));
    EXPECT_EQ(hosts[1]->coarseHealth(), Host::Health::Healthy);
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

// Verify the add and removal of hosts with disable active hc flag during eds update.
TEST_F(EdsTest, DisableActiveHCEndpoints) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  resetCluster();
  initialize();

  auto health_checker = std::make_shared<MockHealthChecker>();
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_)).Times(2);
  cluster_->setHealthChecker(health_checker);

  auto add_endpoint = [&cluster_load_assignment](int port, bool disable_hc, bool healthy) {
    auto* lb_endpoint = cluster_load_assignment.add_endpoints()->add_lb_endpoints();
    auto* endpoint = lb_endpoint->mutable_endpoint();
    auto* socket_address = endpoint->mutable_address()->mutable_socket_address();
    socket_address->set_address("1.2.3.4");
    socket_address->set_port_value(port);
    endpoint->mutable_health_check_config()->set_disable_active_health_check(disable_hc);
    if (disable_hc) {
      if (healthy) {
        lb_endpoint->set_health_status(envoy::config::core::v3::HEALTHY);
      } else {
        lb_endpoint->set_health_status(envoy::config::core::v3::TIMEOUT);
      }
    }
  };

  // First endpoint with disabled active HC.
  add_endpoint(80, true, false);
  // Second endpoint with enabled active HC.
  add_endpoint(81, false, false);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 2);

    // The endpoint with disabled active health check should not be set FAILED_ACTIVE_HC
    // and PENDING_ACTIVE_HC at beginning.
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
    EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_EDS_HEALTH));
    EXPECT_TRUE(hosts[1]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));

    EXPECT_EQ(Host::Health::Unhealthy, hosts[0]->coarseHealth());
    EXPECT_EQ(Host::Health::Unhealthy, hosts[1]->coarseHealth());

    // Remove the pending HC & mark the second host as healthy.
    // This is normally done by the health checker.
    hosts[1]->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);
    hosts[1]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);

    // After the active health check status is changed, run the callbacks to reload hosts.
    health_checker->runCallbacks(hosts[1], HealthTransition::Changed);

    auto& hosts_reload = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts_reload.size(), 2);
    EXPECT_EQ(Host::Health::Healthy, hosts_reload[1]->coarseHealth());
    EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  }

  // Now mark the port 80 endpoint as healthy through EDS, no change for the other one.
  cluster_load_assignment.clear_endpoints();
  add_endpoint(80, true, true);
  add_endpoint(81, false, false);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  HostSharedPtr removed_host;
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 2);
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_EDS_HEALTH));
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));

    removed_host = hosts[1];
    EXPECT_FALSE(hosts[1]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
    EXPECT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  }

  // Disable active health check for both endpoints.
  cluster_load_assignment.clear_endpoints();
  add_endpoint(80, true, true);
  add_endpoint(81, true, true);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    // Both hosts should be present, and both should not be PENDING_DYNAMIC_REMOVAL.
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 2);
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_EDS_HEALTH));
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));

    // Verify that we have a new host. The host is removed even it is active
    // healthy when the active hc flag is changed.
    EXPECT_EQ(removed_host->address()->asString(), hosts[1]->address()->asString());
    EXPECT_NE(removed_host, hosts[1]);
    EXPECT_FALSE(hosts[1]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
    EXPECT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  }

  // Enable the active health check for the port 80 endpoint.
  cluster_load_assignment.clear_endpoints();
  add_endpoint(80, false, true);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);
    EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  }
}

// Validate that onConfigUpdate() removes endpoints that are marked as healthy
// when configured to drain on host removal.
TEST_F(EdsTest, EndpointRemovalClusterDrainOnHostRemoval) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
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
TEST_F(EdsTest, EndpointMovedToNewPriorityWithDrain) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
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

  // Verify that no hosts gets added or removed to/from the PrioritySet.
  auto member_update_cb =
      cluster_->prioritySet().addMemberUpdateCb([&](const auto& added, const auto& removed) {
        EXPECT_TRUE(added.empty());
        EXPECT_TRUE(removed.empty());
      });

  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);

    // assert that it didn't move
    EXPECT_EQ(hosts[0]->address()->asString(), "1.2.3.4:81");

    // The endpoint was healthy in the original priority, so moving it
    // around should preserve that.
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
  }

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[1]->hosts();
    EXPECT_EQ(hosts.size(), 1);

    // assert that it moved
    EXPECT_EQ(hosts[0]->address()->asString(), "1.2.3.4:80");

    // The endpoint was healthy in the original priority, so moving it
    // around should preserve that.
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
  }

  // Moves all the endpoints to priority 1.
  cluster_load_assignment.clear_endpoints();
  add_endpoint(80, 1);
  add_endpoint(81, 1);

  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    // Priority 0 should now be empty.
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 0);
  }

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[1]->hosts();
    EXPECT_EQ(hosts.size(), 2);

    // The endpoints were healthy, so moving them around should preserve that.
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
    EXPECT_FALSE(hosts[1]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_FALSE(hosts[1]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
  }
}

// Verifies that if an endpoint is moved between priorities, the health check value
// of the host is preserved
TEST_F(EdsTest, EndpointMovedWithDrain) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
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
  auto member_update_cb =
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
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
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
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
  }
}

// Verifies that if an endpoint is moved to a new priority, the active hc status is preserved.
TEST_F(EdsTest, EndpointMovedToNewPriority) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  resetCluster();

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

  // Verify that no hosts gets added or removed to/from the PrioritySet.
  auto member_update_cb =
      cluster_->prioritySet().addMemberUpdateCb([&](const auto& added, const auto& removed) {
        EXPECT_TRUE(added.empty());
        EXPECT_TRUE(removed.empty());
      });

  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);

    // assert that it didn't move
    EXPECT_EQ(hosts[0]->address()->asString(), "1.2.3.4:81");

    // The endpoint was healthy in the original priority, so moving it
    // around should preserve that.
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
  }

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[1]->hosts();
    EXPECT_EQ(hosts.size(), 1);

    // assert that it moved
    EXPECT_EQ(hosts[0]->address()->asString(), "1.2.3.4:80");

    // The endpoint was healthy in the original priority, so moving it
    // around should preserve that.
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
  }

  // Moves all the endpoints to priority 1.
  cluster_load_assignment.clear_endpoints();
  add_endpoint(80, 1);
  add_endpoint(81, 1);

  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    // Priority 0 should now be empty.
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 0);
  }

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[1]->hosts();
    EXPECT_EQ(hosts.size(), 2);

    // The endpoints were healthy, so moving them around should preserve that.
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
    EXPECT_FALSE(hosts[1]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_FALSE(hosts[1]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
  }
}

// Verifies that if an endpoint is moved between priorities, the health check value
// of the host is preserved
TEST_F(EdsTest, EndpointMoved) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  resetCluster();

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
  auto member_update_cb =
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
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
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
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
  }
}

// Verifies that if an endpoint is moved to a new priority and has its health check address altered
// then nothing bad happens
TEST_F(EdsTest, EndpointMovedToNewPriorityWithHealthAddressChange) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  resetCluster();

  auto health_checker = std::make_shared<MockHealthChecker>();
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_)).Times(2);
  cluster_->setHealthChecker(health_checker);

  auto add_endpoint = [&cluster_load_assignment](int port, int priority, int health_port) {
    auto* endpoints = cluster_load_assignment.add_endpoints();
    endpoints->set_priority(priority);
    auto* endpoint = endpoints->add_lb_endpoints()->mutable_endpoint();

    auto* socket_address = endpoint->mutable_address()->mutable_socket_address();
    socket_address->set_address("1.2.3.4");
    socket_address->set_port_value(port);

    endpoint->mutable_health_check_config()->set_port_value(health_port);
  };

  add_endpoint(80, 0, 80);
  add_endpoint(81, 1, 81);

  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);

    EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    hosts[0]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
    hosts[0]->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);
  }

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[1]->hosts();
    EXPECT_EQ(hosts.size(), 1);

    EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    hosts[0]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
    hosts[0]->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);
  }

  cluster_load_assignment.clear_endpoints();
  add_endpoint(80, 0, 80);
  add_endpoint(81, 0, 82);

  // Changing a health check endpoint at the same time as priority is an add and immediate remove
  auto member_update_cb =
      cluster_->prioritySet().addMemberUpdateCb([&](const auto& added, const auto& removed) {
        EXPECT_EQ(added.size(), 1);
        EXPECT_EQ(removed.size(), 1);
      });

  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 2);

    EXPECT_EQ(hosts[1]->address()->asString(), "1.2.3.4:81");
    EXPECT_TRUE(hosts[1]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    hosts[1]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
    hosts[1]->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);
  }

  cluster_load_assignment.clear_endpoints();
  add_endpoint(80, 0, 80);
  add_endpoint(81, 1, 83);

  // Changing a health check endpoint at the same time as priority is an add and immediate remove
  auto member_update_cb2 =
      cluster_->prioritySet().addMemberUpdateCb([&](const auto& added, const auto& removed) {
        EXPECT_EQ(added.size(), 1);
        EXPECT_EQ(removed.size(), 1);
      });

  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);
  }

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[1]->hosts();
    EXPECT_EQ(hosts.size(), 1);

    EXPECT_EQ(hosts[0]->address()->asString(), "1.2.3.4:81");
    EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  }
}

// Verifies that if an endpoint's locality is updated, the active hc flags are preserved,
// unless the health checker is updated.
TEST_F(EdsTest, ActiveHealthCheckFlagsEndpointMovedToNewLocality) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  resetCluster();

  auto health_checker = std::make_shared<MockHealthChecker>();
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_)).Times(2);
  cluster_->setHealthChecker(health_checker);

  auto* endpoints = cluster_load_assignment.add_endpoints();
  auto* endpoint = endpoints->add_lb_endpoints()->mutable_endpoint();
  auto* socket_address = endpoint->mutable_address()->mutable_socket_address();
  socket_address->set_address("1.2.3.4");
  socket_address->set_port_value(80);

  // Start with an endpoint in locality zone1, it should be marked FAILED_ACTIVE_HC.
  endpoints->mutable_locality()->set_zone("zone1");
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);
    EXPECT_EQ(hosts[0]->locality().zone(), "zone1");

    // When active-HC is used, the state is initialized to FAILED_ACTIVE_HC.
    EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  }

  // Verify that the host is added (to a new locality) and removed (from its
  // current locality) as part of an update.
  auto member_update_cb =
      cluster_->prioritySet().addMemberUpdateCb([&](const auto& added, const auto& removed) {
        EXPECT_EQ(1, added.size());
        EXPECT_EQ(1, removed.size());
      });

  // Validate that moving an healthy endpoint to another locality keeps
  // it as healthy.
  {
    // Set the endpoint in healthy status, and update its zone.
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    // Mark the host as healthy.
    hosts[0]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  }
  endpoints->mutable_locality()->set_zone("zone2");
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts[0]->locality().zone(), "zone2");

    // The endpoint was healthy in the previous locality, so moving it
    // to another locality should preserve that.
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  }

  // Validate that moving an unhealthy endpoint to another locality keeps
  // it as unhealthy.
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    hosts[0]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  }
  endpoints->mutable_locality()->set_zone("zone3");
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);
    EXPECT_EQ(hosts[0]->locality().zone(), "zone3");

    // The endpoint was in failed status in the previous priority, so moving it
    // to another locality should preserve that.
    EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  }

  // Validate that moving a degraded endpoint to another locality keeps
  // it as degraded.
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    hosts[0]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
    hosts[0]->healthFlagSet(Host::HealthFlag::DEGRADED_ACTIVE_HC);
  }
  endpoints->mutable_locality()->set_zone("zone4");
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);
    EXPECT_EQ(hosts[0]->locality().zone(), "zone4");

    // The endpoint was in degraded status in the original locality, so moving it
    // to another locality should preserve that.
    EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC));
  }

  // Validate that moving a endpoint marked as timeout active health check to
  // another locality keeps that flag.
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    // The code requires that ACTIVE_HC_TIMEOUT is set only if FAILED_ACTIVE_HC
    // is also set.
    hosts[0]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
    hosts[0]->healthFlagSet(Host::HealthFlag::ACTIVE_HC_TIMEOUT);
  }
  endpoints->mutable_locality()->set_zone("zone5");
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);
    EXPECT_EQ(hosts[0]->locality().zone(), "zone5");

    // The endpoint was in timeout status in the original locality, so moving it
    // to another locality keeps the flag..
    EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::ACTIVE_HC_TIMEOUT));
    EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  }

  // Validate that moving a endpoint marked as pending active health check to
  // another locality keeps that flag.
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    // The code requires that PENDING_ACTIVE_HC is set only if FAILED_ACTIVE_HC
    // is also set.
    hosts[0]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
    hosts[0]->healthFlagSet(Host::HealthFlag::PENDING_ACTIVE_HC);
    hosts[0]->healthFlagClear(Host::HealthFlag::ACTIVE_HC_TIMEOUT);
  }
  endpoints->mutable_locality()->set_zone("zone6");
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);
    EXPECT_EQ(hosts[0]->locality().zone(), "zone6");

    // The endpoint was in timeout status in the original priority, but its
    // health checker host changes, so now it is initialized to unhealthy.
    EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC));
    EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  }

  // Validate that updating the locality and the health checker of a healthy
  // endpoint, marks it as failing active HC.
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    hosts[0]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
    hosts[0]->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);
  }
  endpoints->mutable_locality()->set_zone("zone7");
  endpoint->mutable_health_check_config()->set_port_value(90);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);
    EXPECT_EQ(hosts[0]->locality().zone(), "zone7");

    // The endpoint was in healthy status in the original priority, but its
    // health checker host changes, so now it is initialized to unhealthy.
    EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  }
}

// Validates that we correctly update the host list when a new overprovisioning factor is set.
TEST_F(EdsTest, EndpointAddedWithNewOverprovisioningFactor) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
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
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
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
}

TEST_F(EdsTest, EndpointCombineDuplicateLocalities) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");

  auto* endpoints1 = cluster_load_assignment.add_endpoints();
  auto* locality1 = endpoints1->mutable_locality();
  locality1->set_region("oceania");
  locality1->set_zone("hello");
  locality1->set_sub_zone("world");

  auto* endpoints2 = cluster_load_assignment.add_endpoints();
  auto* locality2 = endpoints2->mutable_locality();
  locality2->set_region("oceania");
  locality2->set_zone("hello");
  locality2->set_sub_zone("world");

  {
    auto* endpoint_address = endpoints1->add_lb_endpoints()
                                 ->mutable_endpoint()
                                 ->mutable_address()
                                 ->mutable_socket_address();
    endpoint_address->set_address("1.2.3.4");
    endpoint_address->set_port_value(80);
  }
  {
    auto* endpoint_address = endpoints2->add_lb_endpoints()
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
}

// Validate that onConfigUpdate() updates the endpoint locality of an existing endpoint.
TEST_F(EdsTest, EndpointLocalityUpdated) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
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

  // Update locality now
  locality->set_region("space");
  locality->set_zone("station");
  locality->set_sub_zone("mars");
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  auto& updatedHosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(updatedHosts.size(), 2);
  for (int i = 0; i < 2; ++i) {
    EXPECT_EQ(0, updatedHosts[i]->priority());
    const auto& locality = updatedHosts[i]->locality();
    EXPECT_EQ("space", locality.region());
    EXPECT_EQ("station", locality.zone());
    EXPECT_EQ("mars", locality.sub_zone());
  }
}

// Validate that onConfigUpdate() does not propagate locality weights to the host set when
// locality weighted balancing isn't configured and the cluster does not use LB policy extensions.
TEST_F(EdsTest, EndpointLocalityWeightsIgnored) {
  TestScopedRuntime runtime;
  runtime.mergeValues({{"envoy.reloadable_features.convert_legacy_lb_config", "false"}});

  // Reset the cluster after the runtime change.
  resetCluster();

  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
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

class EdsLocalityWeightsTest : public EdsTest {
public:
  void expectLocalityWeightsPresentForClusterConfig(const std::string& config) {
    envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
    cluster_load_assignment.set_cluster_name("fare");
    resetCluster(config, Cluster::InitializePhase::Secondary);

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
};

// Validate that onConfigUpdate() propagates locality weights to the host set when locality
// weighted balancing is configured.
TEST_F(EdsLocalityWeightsTest, WeightsPresentWithLocalityWeightedConfig) {
  expectLocalityWeightsPresentForClusterConfig(R"EOF(
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
    )EOF");
}

// Validate that onConfigUpdate() propagates locality weights to the host set when the cluster uses
// load balancing policy extensions.
TEST_F(EdsLocalityWeightsTest, WeightsPresentWithLoadBalancingPolicyConfig) {
  // envoy.load_balancers.custom_lb is registered by linking in
  // //test/integration/load_balancers:custom_lb_policy.
  expectLocalityWeightsPresentForClusterConfig(R"EOF(
      name: name
      connect_timeout: 0.25s
      type: EDS
      lb_policy: LOAD_BALANCING_POLICY_CONFIG
      load_balancing_policy:
        policies:
        - typed_extension_config:
            name: envoy.load_balancers.custom_lb
            typed_config:
              "@type": type.googleapis.com/test.integration.custom_lb.CustomLbConfig
      eds_cluster_config:
        service_name: fare
        eds_config:
          api_config_source:
            api_type: REST
            cluster_names:
            - eds
            refresh_delay: 1s
    )EOF");
}

// Validate that onConfigUpdate() removes any locality not referenced in the
// config update in each priority.
TEST_F(EdsTest, RemoveUnreferencedLocalities) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
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
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
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
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
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
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
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

  // Add 2 more hosts to priority 0, and add five hosts to priority 3.
  // Note the (illegal) gap (no priority 2.)  Until we have config validation,
  // make sure bad config does no harm.
  add_hosts_to_priority(0, 2);
  add_hosts_to_priority(3, 5);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  ASSERT_EQ(4, cluster_->prioritySet().hostSetsPerPriority().size());
  EXPECT_EQ(4, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(1, cluster_->prioritySet().hostSetsPerPriority()[1]->hosts().size());
  EXPECT_EQ(0, cluster_->prioritySet().hostSetsPerPriority()[2]->hosts().size());
  EXPECT_EQ(5, cluster_->prioritySet().hostSetsPerPriority()[3]->hosts().size());

  // Update the number of hosts in priority 3. Make sure we clear out the priorities previously
  // occupied by hosts.
  cluster_load_assignment.clear_endpoints();
  add_hosts_to_priority(3, 4);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  ASSERT_EQ(4, cluster_->prioritySet().hostSetsPerPriority().size());
  EXPECT_EQ(0, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0, cluster_->prioritySet().hostSetsPerPriority()[1]->hosts().size());
  EXPECT_EQ(0, cluster_->prioritySet().hostSetsPerPriority()[2]->hosts().size());
  EXPECT_EQ(4, cluster_->prioritySet().hostSetsPerPriority()[3]->hosts().size());
}

// Make sure config updates with P!=0 are rejected for the local cluster.
TEST_F(EdsTest, NoPriorityForLocalCluster) {
  server_context_.cluster_manager_.local_cluster_name_ = "name";
  resetCluster();

  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
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

  // Set up the priority levels so 0 has two hosts and 1 has one host. Update
  // should fail.
  add_hosts_to_priority(0, 2);
  add_hosts_to_priority(1, 1);
  initialize();
  const auto decoded_resources =
      TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
  EXPECT_THROW_WITH_MESSAGE(
      EXPECT_TRUE(eds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "").ok()),
      EnvoyException, "Unexpected non-zero priority for local cluster 'name'.");

  // Try an update which only has endpoints with P=0. This should go through.
  cluster_load_assignment.clear_endpoints();
  add_hosts_to_priority(0, 2);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
}

// Set up an EDS config with multiple priorities and localities and make sure
// they are loaded and reloaded as expected.
TEST_F(EdsTest, PriorityAndLocality) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
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
  EXPECT_EQ(0UL,
            stats_.findCounterByString("cluster.name.update_no_rebuild").value().get().value());

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
  EXPECT_EQ(1UL,
            stats_.findCounterByString("cluster.name.update_no_rebuild").value().get().value());

  // Adjust locality weights, validate that we observe an update.
  cluster_load_assignment.mutable_endpoints(0)->mutable_load_balancing_weight()->set_value(60);
  cluster_load_assignment.mutable_endpoints(1)->mutable_load_balancing_weight()->set_value(40);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_EQ(1UL,
            stats_.findCounterByString("cluster.name.update_no_rebuild").value().get().value());
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
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  auto* endpoints = cluster_load_assignment.add_endpoints();

  auto* endpoint = endpoints->add_lb_endpoints();
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address(
      "foo.bar.com");
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);

  initialize();
  const auto decoded_resources =
      TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
  EXPECT_THROW_WITH_MESSAGE(
      EXPECT_TRUE(eds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "").ok()),
      EnvoyException,
      "malformed IP address: foo.bar.com. Consider setting resolver_name or "
      "setting cluster type to 'STRICT_DNS' or 'LOGICAL_DNS'");
}

class EdsAssignmentTimeoutTest : public EdsTest {
public:
  EdsAssignmentTimeoutTest() {
    EXPECT_CALL(server_context_.dispatcher_, createTimer_(_))
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
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
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

  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment_lease =
      cluster_load_assignment;
  cluster_load_assignment_lease.mutable_policy()->mutable_endpoint_stale_after()->MergeFrom(
      Protobuf::util::TimeUtil::SecondsToDuration(1));

  EXPECT_CALL(*interval_timer_, enableTimer(_, _)).Times(2); // Timer enabled twice.
  EXPECT_CALL(*interval_timer_, disableTimer());             // Timer disabled once.
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
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
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
  EXPECT_CALL(*interval_timer_, enabled());
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

// Validates that assignment timeout is disabled when an update arrives.
TEST_F(EdsAssignmentTimeoutTest, AssignmentLeaseUpdateDisablesTimer) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
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
  EXPECT_CALL(*interval_timer_, enabled());
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(1000), _));
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 2);
  }

  // Update the assignment, expect the old timer to be disabled.
  cluster_load_assignment.mutable_endpoints(0)->mutable_load_balancing_weight()->set_value(31);
  EXPECT_CALL(*interval_timer_, enabled());
  EXPECT_CALL(*interval_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(1000), _));
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
}

// Validate that onConfigUpdate() with a config that contains both LEDS config
// source and explicit list of endpoints is rejected.
TEST_F(EdsTest, OnConfigUpdateLedsAndEndpoints) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  // Add an endpoint.
  auto* endpoints = cluster_load_assignment.add_endpoints();
  auto* endpoint = endpoints->add_lb_endpoints();
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("1.2.3.4");
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);
  // Configure an LEDS data source.
  auto* leds_conf = endpoints->mutable_leds_cluster_locality_config();
  leds_conf->set_leds_collection_name("xdstp://foo/leds/collection");
  initialize();

  const auto decoded_resources =
      TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
  EXPECT_EQ(eds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "").message(),
            "A ClusterLoadAssignment for cluster fare cannot include both LEDS "
            "(resource: xdstp://foo/leds/collection) and a list of endpoints.");
}

class EdsCachedAssignmentTest : public testing::Test {
public:
  EdsCachedAssignmentTest() {
    // TODO(adisuissa): setting the runtime guard is done because the runtime
    // guard is false by default. The runtime environment should be removed
    // once this guard is removed.
    runtime_.mergeValues({{"envoy.restart_features.use_eds_cache_for_ads", "true"}});
    resetCluster();
  }

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

  void resetCluster(const std::string& yaml_config, Cluster::InitializePhase initialize_phase) {
    server_context_.local_info_.node_.mutable_locality()->set_zone("us-east-1a");
    EXPECT_CALL(server_context_.dispatcher_, createTimer_(_))
        .WillOnce(Invoke([this](Event::TimerCb cb) {
          timer_cb_pre_ = cb;
          EXPECT_EQ(nullptr, interval_timer_pre_);
          interval_timer_pre_ = new Event::MockTimer();
          return interval_timer_pre_;
        }))
        .WillRepeatedly(Invoke([](Event::TimerCb) { return new Event::MockTimer(); }));

    eds_cluster_ = parseClusterFromV3Yaml(yaml_config);
    Envoy::Upstream::ClusterFactoryContextImpl factory_context(
        server_context_, server_context_.cluster_manager_, nullptr, ssl_context_manager_, nullptr,
        false);
    ON_CALL(server_context_.cluster_manager_, edsResourcesCache())
        .WillByDefault(
            Invoke([this]() -> Config::EdsResourcesCacheOptRef { return eds_resources_cache_; }));
    cluster_pre_ = std::make_shared<EdsClusterImpl>(eds_cluster_, factory_context);
    EXPECT_EQ(initialize_phase, cluster_pre_->initializePhase());
    eds_callbacks_pre_ = server_context_.cluster_manager_.subscription_factory_.callbacks_;
  }

  void initialize() {
    EXPECT_CALL(*server_context_.cluster_manager_.subscription_factory_.subscription_, start(_));
    cluster_pre_->initialize([this] { initialized_ = true; });
  }

  void doOnConfigUpdateVerifyNoThrowPre(
      const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment) {
    const auto decoded_resources =
        TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
    EXPECT_TRUE(eds_callbacks_pre_->onConfigUpdate(decoded_resources.refvec_, "").ok());
  }

  void doOnConfigUpdateVerifyNoThrowPost(
      const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment) {
    const auto decoded_resources =
        TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
    EXPECT_TRUE(eds_callbacks_post_->onConfigUpdate(decoded_resources.refvec_, "").ok());
  }
  // Emulates a CDS update that creates a new cluster object with the same name,
  // that waits for EDS response.
  void updateCluster() {
    EXPECT_CALL(server_context_.dispatcher_, createTimer_(_))
        .WillOnce(Invoke([this](Event::TimerCb cb) {
          timer_cb_post_ = cb;
          EXPECT_EQ(nullptr, interval_timer_post_);
          interval_timer_post_ = new Event::MockTimer();
          return interval_timer_post_;
        }))
        .WillRepeatedly(Invoke([](Event::TimerCb) { return new Event::MockTimer(); }));

    Envoy::Upstream::ClusterFactoryContextImpl factory_context(
        server_context_, server_context_.cluster_manager_, nullptr, ssl_context_manager_, nullptr,
        false);
    cluster_post_ = std::make_shared<EdsClusterImpl>(eds_cluster_, factory_context);
    // EXPECT_EQ(initialize_phase, cluster_post_->initializePhase());
    eds_callbacks_post_ = server_context_.cluster_manager_.subscription_factory_.callbacks_;

    EXPECT_CALL(*server_context_.cluster_manager_.subscription_factory_.subscription_, start(_));
    cluster_post_->initialize([this] { initialized_post_ = true; });
  }

  // Used for timeout emulation.
  Event::MockTimer* interval_timer_pre_{nullptr};
  Event::TimerCb timer_cb_pre_;
  Event::MockTimer* interval_timer_post_{nullptr};
  Event::TimerCb timer_cb_post_;

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  bool initialized_{};
  bool initialized_post_{};
  Stats::TestUtil::TestStore& stats_ = server_context_.store_;
  NiceMock<Ssl::MockContextManager> ssl_context_manager_;
  envoy::config::cluster::v3::Cluster eds_cluster_;
  NiceMock<Random::MockRandomGenerator> random_;
  // TestScopedRuntime runtime_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Config::MockEdsResourcesCache eds_resources_cache_;
  TestScopedRuntime runtime_;

  // EDS caching works when a cluster update occurs (a CDS update arrives, a new
  // cluster with the same name is created, but the EDS response doesn't
  // arrive). We emulate this by having 2 EdsCluster instance cluster_pre_ for
  // the initial cluster, and cluster_post_ for the one created after the cluster update.
  EdsClusterImplSharedPtr cluster_pre_;
  EdsClusterImplSharedPtr cluster_post_;
  Config::SubscriptionCallbacks* eds_callbacks_pre_{};
  Config::SubscriptionCallbacks* eds_callbacks_post_{};
};

// Validates that cached assignments are not used if an EDS update for a cluster arrives.
TEST_F(EdsCachedAssignmentTest, ClusterUpdateNotUsingCacheOnEdsUpdate) {
  // Set an initial assignment.
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  auto* endpoints = cluster_load_assignment.add_endpoints();
  auto* endpoint = endpoints->add_lb_endpoints();
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("1.2.3.4");
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);
  endpoint->mutable_load_balancing_weight()->set_value(10);

  initialize();
  // No call to the cache to fetch the assignment, as it is being delivered as expected.
  EXPECT_CALL(eds_resources_cache_, getResource("fare", _)).Times(0);
  EXPECT_CALL(*interval_timer_pre_, enabled());
  doOnConfigUpdateVerifyNoThrowPre(cluster_load_assignment);
  EXPECT_TRUE(initialized_);
  {
    const auto& hosts = cluster_pre_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);
    EXPECT_EQ(hosts[0]->weight(), 10);
  }

  // Update the cluster, and send an updated assignment back.
  // Still no call to the cache to fetch the assignment, as it is being delivered as expected.
  EXPECT_CALL(eds_resources_cache_, getResource("fare", _)).Times(0);
  updateCluster();
  {
    endpoint->mutable_load_balancing_weight()->set_value(11);
    EXPECT_CALL(*interval_timer_post_, enabled());
    doOnConfigUpdateVerifyNoThrowPost(cluster_load_assignment);

    const auto& hosts = cluster_post_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);
    EXPECT_EQ(hosts[0]->weight(), 11);
  }
}

// Validates that cached assignments are used if no EDS update for a cluster arrives
// (i.e., EDS-Timeout).
TEST_F(EdsCachedAssignmentTest, UseCachedAssignmentOnWarmingFailure) {
  // Set an initial assignment.
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  auto* endpoints = cluster_load_assignment.add_endpoints();
  auto* endpoint = endpoints->add_lb_endpoints();
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("1.2.3.4");
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);
  endpoint->mutable_load_balancing_weight()->set_value(10);

  // Store in the cache an assignment with a different weight.
  envoy::config::endpoint::v3::ClusterLoadAssignment cached_cluster_load_assignment;
  cached_cluster_load_assignment.CopyFrom(cluster_load_assignment);
  cached_cluster_load_assignment.mutable_endpoints(0)
      ->mutable_lb_endpoints(0)
      ->mutable_load_balancing_weight()
      ->set_value(22);

  initialize();
  // No call to the cache to fetch the assignment, as it is being delivered as expected.
  EXPECT_CALL(eds_resources_cache_, getResource("fare", _)).Times(0);
  EXPECT_CALL(*interval_timer_pre_, enabled());
  doOnConfigUpdateVerifyNoThrowPre(cluster_load_assignment);
  EXPECT_TRUE(initialized_);
  {
    const auto& hosts = cluster_pre_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);
    EXPECT_EQ(hosts[0]->weight(), 10);
  }

  // Update the cluster and emulate a warming failure, and validate
  // that the resource is fetched from the cache.
  updateCluster();
  {
    EnvoyException dummy_ex("dummy exception");
    EXPECT_CALL(eds_resources_cache_, getResource("fare", _))
        .WillOnce(Return(cached_cluster_load_assignment));
    eds_callbacks_post_->onConfigUpdateFailed(
        Envoy::Config::ConfigUpdateFailureReason::FetchTimedout, &dummy_ex);
    const auto& hosts = cluster_post_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);
    EXPECT_EQ(hosts[0]->weight(), 22);
  }
  // Removing the cluster on test d'tor will trigger removeCallback.
  EXPECT_CALL(eds_resources_cache_, removeCallback("fare", _));
}

// Validates that no cached assignments are used if no EDS update for a cluster arrives.
// This test should be deleted once the enable_eds_cache runtime flag is removed.
TEST_F(EdsCachedAssignmentTest, UseCachedAssignmentOnWarmingFailureNoCache) {
  // TODO(adisuissa): this test should be removed once the runtime guard is deprecated.
  runtime_.mergeValues({{"envoy.restart_features.use_eds_cache_for_ads", "false"}});
  // Set an initial assignment.
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  auto* endpoints = cluster_load_assignment.add_endpoints();
  auto* endpoint = endpoints->add_lb_endpoints();
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("1.2.3.4");
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);
  endpoint->mutable_load_balancing_weight()->set_value(10);

  // Store in the cache an assignment with a different weight.
  envoy::config::endpoint::v3::ClusterLoadAssignment cached_cluster_load_assignment;
  cached_cluster_load_assignment.CopyFrom(cluster_load_assignment);
  cached_cluster_load_assignment.mutable_endpoints(0)
      ->mutable_lb_endpoints(0)
      ->mutable_load_balancing_weight()
      ->set_value(22);

  initialize();
  // No call to the cache to fetch the assignment, as it is being delivered as expected.
  EXPECT_CALL(eds_resources_cache_, getResource("fare", _)).Times(0);
  EXPECT_CALL(*interval_timer_pre_, enabled());
  doOnConfigUpdateVerifyNoThrowPre(cluster_load_assignment);
  EXPECT_TRUE(initialized_);
  {
    const auto& hosts = cluster_pre_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);
    EXPECT_EQ(hosts[0]->weight(), 10);
  }

  // Update the cluster and emulate a warming failure, and validate
  // that the resource is not fetched from the cache because caching is disabled.
  updateCluster();
  {
    EnvoyException dummy_ex("dummy exception");
    EXPECT_CALL(eds_resources_cache_, getResource("fare", _)).Times(0);
    eds_callbacks_post_->onConfigUpdateFailed(
        Envoy::Config::ConfigUpdateFailureReason::FetchTimedout, &dummy_ex);
    const auto& hosts = cluster_post_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 0);
  }
}

// Validates that after using a cached assignment, and receiving an update for it, the
// updated assignment is used.
TEST_F(EdsCachedAssignmentTest, CachedAssignmentUpdate) {
  // Set an initial assignment.
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  cluster_load_assignment.mutable_policy()->mutable_endpoint_stale_after()->MergeFrom(
      Protobuf::util::TimeUtil::SecondsToDuration(1));
  auto* endpoints = cluster_load_assignment.add_endpoints();
  auto* endpoint = endpoints->add_lb_endpoints();
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("1.2.3.4");
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);
  endpoint->mutable_load_balancing_weight()->set_value(10);

  // Store in the cache an assignment with a different weight.
  envoy::config::endpoint::v3::ClusterLoadAssignment cached_cluster_load_assignment;
  cached_cluster_load_assignment.CopyFrom(cluster_load_assignment);
  cached_cluster_load_assignment.mutable_endpoints(0)
      ->mutable_lb_endpoints(0)
      ->mutable_load_balancing_weight()
      ->set_value(22);

  initialize();
  // Expect the timer to be enabled once.
  EXPECT_CALL(*interval_timer_pre_, enableTimer(std::chrono::milliseconds(1000), _));
  EXPECT_CALL(*interval_timer_pre_, enabled());
  EXPECT_CALL(eds_resources_cache_, setExpiryTimer("fare", std::chrono::milliseconds(1000)));
  doOnConfigUpdateVerifyNoThrowPre(cluster_load_assignment);
  EXPECT_TRUE(initialized_);
  {
    const auto& hosts = cluster_pre_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);
    EXPECT_EQ(hosts[0]->weight(), 10);
  }

  Config::EdsResourceRemovalCallback* removal_cb = nullptr;
  // Update the cluster and emulate a warming failure, and validate
  // that the resource is fetched from the cache.
  updateCluster();
  {
    EnvoyException dummy_ex("dummy exception");
    EXPECT_CALL(eds_resources_cache_, getResource("fare", _))
        .WillOnce(DoAll(SaveArg<1>(&removal_cb), Return(cached_cluster_load_assignment)));
    eds_callbacks_post_->onConfigUpdateFailed(
        Envoy::Config::ConfigUpdateFailureReason::FetchTimedout, &dummy_ex);
    const auto& hosts = cluster_post_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);
    EXPECT_EQ(hosts[0]->weight(), 22);
  }

  // Send a successful update to the cluster.
  // No call for fetching the resource from the cache.
  EXPECT_CALL(eds_resources_cache_, getResource("fare", _)).Times(0);
  {
    endpoint->mutable_load_balancing_weight()->set_value(11);
    EXPECT_CALL(eds_resources_cache_, removeCallback("fare", removal_cb));
    // Expect the timer to be enabled once.
    EXPECT_CALL(*interval_timer_post_, enableTimer(std::chrono::milliseconds(1000), _));
    // Expect the timer to be disabled when stale assignments are removed.
    // EXPECT_CALL(*interval_timer_, disableTimer());
    EXPECT_CALL(*interval_timer_post_, enabled());
    EXPECT_CALL(eds_resources_cache_, setExpiryTimer("fare", std::chrono::milliseconds(1000)));
    doOnConfigUpdateVerifyNoThrowPost(cluster_load_assignment);

    const auto& hosts = cluster_post_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);
    EXPECT_EQ(hosts[0]->weight(), 11);
  }
}

// Validates that a used cached assignment that times out invokes the remove callback.
TEST_F(EdsCachedAssignmentTest, CachedAssignmentRemovedOnTimeout) {
  // Set an initial assignment.
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  cluster_load_assignment.mutable_policy()->mutable_endpoint_stale_after()->MergeFrom(
      Protobuf::util::TimeUtil::SecondsToDuration(1));
  auto* endpoints = cluster_load_assignment.add_endpoints();
  auto* endpoint = endpoints->add_lb_endpoints();
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("1.2.3.4");
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);
  endpoint->mutable_load_balancing_weight()->set_value(10);

  // Store in the cache an assignment with a different weight.
  envoy::config::endpoint::v3::ClusterLoadAssignment cached_cluster_load_assignment;
  cached_cluster_load_assignment.CopyFrom(cluster_load_assignment);
  cached_cluster_load_assignment.mutable_endpoints(0)
      ->mutable_lb_endpoints(0)
      ->mutable_load_balancing_weight()
      ->set_value(22);

  initialize();
  // Expect the timer to be enabled once.
  EXPECT_CALL(*interval_timer_pre_, enableTimer(std::chrono::milliseconds(1000), _));
  // Expect the timer to be disabled when stale assignments are removed.
  // EXPECT_CALL(*interval_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_pre_, enabled());
  // No call to the cache to fetch the assignment, as it is being delivered as expected.
  EXPECT_CALL(eds_resources_cache_, getResource("fare", _)).Times(0);
  EXPECT_CALL(eds_resources_cache_, setExpiryTimer("fare", std::chrono::milliseconds(1000)));
  doOnConfigUpdateVerifyNoThrowPre(cluster_load_assignment);
  EXPECT_TRUE(initialized_);
  {
    const auto& hosts = cluster_pre_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);
    EXPECT_EQ(hosts[0]->weight(), 10);
  }

  Config::EdsResourceRemovalCallback* removal_cb = nullptr;
  // Update the cluster and emulate a warming failure, and validate
  // that the resource is fetched from the cache.
  updateCluster();
  {
    EnvoyException dummy_ex("dummy exception");
    EXPECT_CALL(eds_resources_cache_, getResource("fare", _))
        .WillOnce(DoAll(SaveArg<1>(&removal_cb), Return(cached_cluster_load_assignment)));
    EXPECT_CALL(*interval_timer_post_, enabled());
    eds_callbacks_post_->onConfigUpdateFailed(
        Envoy::Config::ConfigUpdateFailureReason::FetchTimedout, &dummy_ex);
    const auto& hosts = cluster_post_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);
    EXPECT_EQ(hosts[0]->weight(), 22);
  }

  // Emulate a timeout.
  EXPECT_CALL(eds_resources_cache_, removeResource("fare"));
  timer_cb_pre_();
  // Emulate a timer expiration call to removal_cb->onCachedResourceRemoved().
  removal_cb->onCachedResourceRemoved("fare");
  // Test that stale endpoints on the updated cluster are removed.
  {
    auto& hosts = cluster_post_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 0);
  }
  // Removing the cluster on test d'tor will trigger removeCallback.
  EXPECT_CALL(eds_resources_cache_, removeCallback("fare", _));
}

} // namespace
} // namespace Upstream
} // namespace Envoy
