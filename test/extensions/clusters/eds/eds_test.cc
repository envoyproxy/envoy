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
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/health_checker.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::Envoy::StatusHelpers::IsOk;
using testing::_;
using testing::DoAll;
using ::testing::Not;
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

  void resetClusterGracefulHostRemoval() {
    resetCluster(R"EOF(
        name: name
        connect_timeout: 0.25s
        type: EDS
        lb_policy: ROUND_ROBIN
        health_checks:
        - timeout: 1s
          interval: 1s
          unhealthy_threshold: 2
          healthy_threshold: 2
          http_health_check:
            path: "/healthcheck"
        eds_cluster_config:
          service_name: fare
          graceful_host_removal: true
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
    Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context_, nullptr, nullptr,
                                                               true);
    cluster_ = *EdsClusterImpl::create(eds_cluster_, factory_context);
    EXPECT_EQ(initialize_phase, cluster_->initializePhase());
    eds_callbacks_ = server_context_.cluster_manager_.subscription_factory_.callbacks_;
  }

  void initialize() {
    EXPECT_CALL(server_context_, timeSource()).WillRepeatedly(testing::ReturnRef(simTime()));
    EXPECT_CALL(*server_context_.cluster_manager_.subscription_factory_.subscription_, start(_));
    cluster_->initialize([this] {
      initialized_ = true;
      return absl::OkStatus();
    });
  }

  void doOnConfigUpdateVerifyNoThrow(
      const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment) {
    const auto decoded_resources =
        TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
    EXPECT_OK(eds_callbacks_->onConfigUpdate(decoded_resources.refvec_, ""));
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  bool initialized_{};
  Stats::TestUtil::TestStore& stats_ = server_context_.store_;

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
  EXPECT_OK(eds_callbacks_->onConfigUpdate({}, ""));
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_OK(eds_callbacks_->onConfigUpdate({}, removed_resources, ""));
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
  std::ignore = resource->mutable_resource()->PackFrom(cluster_load_assignment);
  resource->set_version("v1");
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::endpoint::v3::ClusterLoadAssignment>(
          resources, "cluster_name");
  EXPECT_OK(eds_callbacks_->onConfigUpdate(decoded_resources.refvec_, {}, "v1"));

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
  EXPECT_CALL(*connection, streamInfo());

  Envoy::Upstream::Host::CreateConnectionData connection_data =
      hosts[0]->createConnection(dispatcher, options, transport_socket_options);
  // The created connection will be wrapped in a HappyEyeballsConnectionImpl.
  EXPECT_NE(connection, connection_data.connection_.get());
}

// Verify that non-IP additional addresses are rejected.
TEST_F(EdsTest, RejectNonIpAdditionalAddresses) {
  TestScopedRuntime scoped_runtime;
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");

  // Add dual stack endpoint
  auto* endpoints = cluster_load_assignment.add_endpoints();
  auto* endpoint = endpoints->add_lb_endpoints();
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("::1");
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);
  endpoint->mutable_endpoint()
      ->mutable_additional_addresses()
      ->Add()
      ->mutable_address()
      ->mutable_envoy_internal_address()
      ->set_server_listener_name("internal_address");

  endpoint->mutable_load_balancing_weight()->set_value(30);

  initialize();
  const auto decoded_resources =
      TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.happy_eyeballs_sort_non_ip_addresses", "false"}});
  try {
    (void)eds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "");
    FAIL() << "Invalid address was not rejected";
  } catch (const EnvoyException& e) {
    EXPECT_STREQ("additional_addresses must be IP addresses.", e.what());
  }
}

TEST_F(EdsTest, AllowNonIpAdditionalAddresses) {
  TestScopedRuntime scoped_runtime;
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");

  // Add dual stack endpoint
  auto* endpoints = cluster_load_assignment.add_endpoints();
  auto* endpoint = endpoints->add_lb_endpoints();
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("::1");
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);
  endpoint->mutable_endpoint()
      ->mutable_additional_addresses()
      ->Add()
      ->mutable_address()
      ->mutable_envoy_internal_address()
      ->set_server_listener_name("internal_address");

  endpoint->mutable_load_balancing_weight()->set_value(30);

  initialize();
  const auto decoded_resources =
      TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.happy_eyeballs_sort_non_ip_addresses", "true"}});
  (void)eds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "");

  const auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(hosts[0]->addressListOrNull()->size(), 2);
  EXPECT_EQ((*hosts[0]->addressListOrNull())[0]->asString(), "[::1]:80");
  EXPECT_EQ((*hosts[0]->addressListOrNull())[1]->asString(), "envoy://internal_address/");
}

// Verify that failure to initialize the base class results in an error not a crash.
// Note that this test is depending on the current implementation of how EDS inherits from
// `BaseDynamicClusterImpl` and how `BaseDynamicClusterImpl` does error handling to have a
// failure occur in the base class constructor.
// This is a regression https://github.com/envoyproxy/envoy/pull/39083.
TEST_F(EdsTest, RejectBaseClassConstructorFailure) {
  // Configure an invalid transport socket.
  eds_cluster_ = parseClusterFromV3Yaml(R"EOF(
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
      transport_socket:
        name: envoy.transport_sockets.tls
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
          common_tls_context:
            tls_certificates:
            # Multiple certs are not allowed in a client context.
            - certificate_chain: { filename: "invalid-path" }
              private_key: { filename: "invalid-path" }
            - certificate_chain: { filename: "invalid-path2" }
              private_key: { filename: "invalid-path2" }
 )EOF");
  Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context_, nullptr, nullptr,
                                                             false);
  auto cluster_or_status = EdsClusterImpl::create(eds_cluster_, factory_context);

  // The most important passing criteria is that the above didn't crash.

  EXPECT_THAT(cluster_or_status, Not(IsOk()));
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

// Validate that a subsequent onConfigUpdate() that only changes the hostname for an endpoint with
// an unchanged address recreates the host so the new hostname takes effect. The hostname is
// immutable on a host, so it cannot be updated in place.
TEST_F(EdsTest, HostnameUpdate) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  auto* endpoint = cluster_load_assignment.add_endpoints()->add_lb_endpoints()->mutable_endpoint();
  auto* socket_address = endpoint->mutable_address()->mutable_socket_address();
  socket_address->set_address("1.2.3.4");
  socket_address->set_port_value(1234);
  endpoint->set_hostname("foo");
  cluster_load_assignment.set_cluster_name("fare");
  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  const HostConstSharedPtr original_host =
      cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0];
  EXPECT_EQ(original_host->hostname(), "foo");

  endpoint->set_hostname("bar");
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(hosts.size(), 1);
  EXPECT_EQ(hosts[0]->hostname(), "bar");
  // The host must have been recreated, not updated in place.
  EXPECT_NE(original_host.get(), hosts[0].get());
}

// Validate that a change to the health check hostname override is applied on a subsequent
// onConfigUpdate() by recreating the host. The cluster has an active health checker, so the health
// check hostname is meaningful and a change to it must take effect.
TEST_F(EdsTest, HealthCheckConfigHostnameUpdate) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  auto* endpoint = cluster_load_assignment.add_endpoints()->add_lb_endpoints()->mutable_endpoint();
  auto* socket_address = endpoint->mutable_address()->mutable_socket_address();
  socket_address->set_address("1.2.3.4");
  socket_address->set_port_value(1234);
  endpoint->mutable_health_check_config()->set_hostname("foo");
  cluster_load_assignment.set_cluster_name("fare");
  initialize();

  auto health_checker = std::make_shared<MockHealthChecker>();
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_)).Times(2);
  cluster_->setHealthChecker(health_checker);

  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  const HostConstSharedPtr original_host =
      cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0];
  EXPECT_EQ(original_host->hostnameForHealthChecks(), "foo");

  endpoint->mutable_health_check_config()->set_hostname("bar");
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(hosts.size(), 1);
  EXPECT_EQ(hosts[0]->hostnameForHealthChecks(), "bar");
  EXPECT_NE(original_host.get(), hosts[0].get());
}

// Validate that re-pushing an identical assignment (unchanged hostname) reuses the existing host in
// place, i.e. the hostname comparison does not over-trigger host recreation.
TEST_F(EdsTest, HostnameUnchangedReusesHost) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  auto* endpoint = cluster_load_assignment.add_endpoints()->add_lb_endpoints()->mutable_endpoint();
  auto* socket_address = endpoint->mutable_address()->mutable_socket_address();
  socket_address->set_address("1.2.3.4");
  socket_address->set_port_value(1234);
  endpoint->set_hostname("foo");
  cluster_load_assignment.set_cluster_name("fare");
  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  const HostConstSharedPtr original_host =
      cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0];

  // Re-push the same assignment; the host should be reused, not recreated.
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(hosts.size(), 1);
  EXPECT_EQ(original_host.get(), hosts[0].get());
}

// Validate that when the cluster has no active health checker, a change to the health check
// hostname override does NOT recreate the host.
TEST_F(EdsTest, HealthCheckConfigHostnameUpdateWithoutHealthCheckerReusesHost) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  auto* endpoint = cluster_load_assignment.add_endpoints()->add_lb_endpoints()->mutable_endpoint();
  auto* socket_address = endpoint->mutable_address()->mutable_socket_address();
  socket_address->set_address("1.2.3.4");
  socket_address->set_port_value(1234);
  endpoint->mutable_health_check_config()->set_hostname("foo");
  cluster_load_assignment.set_cluster_name("fare");
  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  const HostConstSharedPtr original_host =
      cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0];
  EXPECT_EQ(original_host->hostnameForHealthChecks(), "foo");

  endpoint->mutable_health_check_config()->set_hostname("bar");
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(hosts.size(), 1);
  // No health checker is configured, so the host should be reused, not recreated.
  EXPECT_EQ(original_host.get(), hosts[0].get());
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
      EXPECT_OK(eds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "")), EnvoyException,
      "malformed IP address: foo.bar.com");
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
    health_checker->runCallbacks(hosts[1], HealthTransition::Changed, HealthState::Unhealthy);
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
    health_checker->runCallbacks(hosts[1], HealthTransition::Changed, HealthState::Healthy);

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

// Batch update that runs a function.
class FunctionBatchUpdate : public PrioritySet::BatchUpdateCb {
public:
  explicit FunctionBatchUpdate(std::function<void()> function) : function_(std::move(function)) {}

  // PrioritySet::BatchUpdateCb
  void batchUpdate(PrioritySet::HostUpdateCb&) override { function_(); }

private:
  const std::function<void()> function_;
};

// Tests of graceful_host_removal. Unless specified otherwise, the endpoints are at 1.2.3.4 and in
// their own LocalityLbEndpoints.
class EdsGracefulHostRemovalTest : public EdsTest {
public:
  EdsGracefulHostRemovalTest() {
    cluster_load_assignment_.set_cluster_name("fare");
    resetClusterGracefulHostRemoval();
  }

  // Initializes the cluster, with a mock active health checker if with_health_checker is true.
  void initializeCluster(bool with_health_checker = true) {
    initialize();
    if (with_health_checker) {
      health_checker_ = std::make_shared<MockHealthChecker>();
      EXPECT_CALL(*health_checker_, start());
      EXPECT_CALL(*health_checker_, addHostCheckCompleteCb(_)).Times(2);
      cluster_->setHealthChecker(health_checker_);
    }
  }

  void addEndpoint(
      uint32_t port, uint32_t priority = 0, uint32_t hc_port = 0, const std::string& region = "",
      uint32_t locality_weight = 0, const std::string& hostname = "",
      envoy::config::core::v3::HealthStatus health_status = envoy::config::core::v3::UNKNOWN) {
    auto* endpoints = cluster_load_assignment_.add_endpoints();
    endpoints->set_priority(priority);
    if (!region.empty()) {
      endpoints->mutable_locality()->set_region(region);
    }
    if (locality_weight != 0) {
      endpoints->mutable_load_balancing_weight()->set_value(locality_weight);
    }
    auto* lb_endpoint = endpoints->add_lb_endpoints();
    lb_endpoint->set_health_status(health_status);
    auto* endpoint = lb_endpoint->mutable_endpoint();
    endpoint->set_hostname(hostname);
    auto* socket_address = endpoint->mutable_address()->mutable_socket_address();
    socket_address->set_address("1.2.3.4");
    socket_address->set_port_value(port);
    if (hc_port != 0) {
      endpoint->mutable_health_check_config()->set_port_value(hc_port);
    }
  }

  // Adds an endpoint in a specific zone (with empty region) for testing local-locality handling.
  void addEndpointInZone(uint32_t port, const std::string& zone, uint32_t locality_weight = 0) {
    auto* endpoints = cluster_load_assignment_.add_endpoints();
    endpoints->mutable_locality()->set_zone(zone);
    if (locality_weight != 0) {
      endpoints->mutable_load_balancing_weight()->set_value(locality_weight);
    }
    auto* socket_address = endpoints->add_lb_endpoints()
                               ->mutable_endpoint()
                               ->mutable_address()
                               ->mutable_socket_address();
    socket_address->set_address("1.2.3.4");
    socket_address->set_port_value(port);
  }

  void resetClusterWithHealthyPanicThreshold(double panic_threshold_percent) {
    resetCluster(fmt::format(R"EOF(
        name: name
        connect_timeout: 0.25s
        type: EDS
        lb_policy: ROUND_ROBIN
        common_lb_config:
          healthy_panic_threshold:
            value: {}
        health_checks:
        - timeout: 1s
          interval: 1s
          unhealthy_threshold: 2
          healthy_threshold: 2
          http_health_check:
            path: "/healthcheck"
        eds_cluster_config:
          service_name: fare
          graceful_host_removal: true
          eds_config:
            api_config_source:
              api_type: REST
              cluster_names:
              - eds
              refresh_delay: 1s
    )EOF",
                             panic_threshold_percent),
                 Cluster::InitializePhase::Secondary);
  }

  void resetRingHashCluster(bool locality_weighted = false) {
    const std::string common_lb_config = R"EOF(
        common_lb_config:
          locality_weighted_lb_config: {}
    )EOF";
    resetCluster(R"EOF(
        name: name
        connect_timeout: 0.25s
        type: EDS
        lb_policy: RING_HASH
        health_checks:
        - timeout: 1s
          interval: 1s
          unhealthy_threshold: 2
          healthy_threshold: 2
          http_health_check:
            path: "/healthcheck"
        eds_cluster_config:
          service_name: fare
          graceful_host_removal: true
          eds_config:
            api_config_source:
              api_type: REST
              cluster_names:
              - eds
              refresh_delay: 1s
    )EOF" + (locality_weighted ? common_lb_config : ""),
                 Cluster::InitializePhase::Secondary);
  }

  // Sets the load balancing weight of the endpoint added last.
  void setLastEndpointWeight(uint32_t weight) {
    cluster_load_assignment_.mutable_endpoints(cluster_load_assignment_.endpoints_size() - 1)
        ->mutable_lb_endpoints(0)
        ->mutable_load_balancing_weight()
        ->set_value(weight);
  }

  ThreadAwareLoadBalancerPtr createThreadAwareLoadBalancer() {
    ThreadAwareLoadBalancerPtr thread_aware_lb = cluster_->info()->loadBalancerFactory().create(
        cluster_->info()->loadBalancerConfig(), *cluster_->info(), cluster_->prioritySet(),
        server_context_.runtime_loader_, server_context_.api_.random_, simTime());
    EXPECT_OK(thread_aware_lb->initialize());
    return thread_aware_lb;
  }

  void updateEndpoints() { doOnConfigUpdateVerifyNoThrow(cluster_load_assignment_); }

  const HostSet& hostSet(uint32_t priority) const {
    return *cluster_->prioritySet().hostSetsPerPriority()[priority];
  }

  HostSharedPtr findHost(const std::string& address) const {
    return cluster_->prioritySet().crossPriorityHostMap()->findHost(address);
  }

  std::vector<std::string> hostAddresses(uint32_t priority) const {
    std::vector<std::string> addresses;
    for (const HostSharedPtr& host : hostSet(priority).hosts()) {
      addresses.push_back(host->address()->asString());
    }
    return addresses;
  }

  // Returns the addresses of the hosts of a priority retained with PENDING_DYNAMIC_REMOVAL.
  std::vector<std::string> retainedHostAddresses(uint32_t priority) const {
    std::vector<std::string> addresses;
    for (const HostSharedPtr& host : hostSet(priority).hosts()) {
      if (host->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL)) {
        addresses.push_back(host->address()->asString());
      }
    }
    return addresses;
  }

  // Marks a host as passing active health checking, as the health checker does.
  void passActiveHealthCheck(const HostSharedPtr& host) {
    host->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
    host->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);
    health_checker_->runCallbacks(host, HealthTransition::Changed, HealthState::Healthy);
  }

  // Marks a host as failing its initial active health check, as the health checker does.
  void failInitialActiveHealthCheck(const HostSharedPtr& host) {
    host->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);
    health_checker_->runCallbacks(host, HealthTransition::Changed, HealthState::Unhealthy);
  }

  void passActiveHealthCheckForAllHosts() {
    for (const auto& host_set : cluster_->prioritySet().hostSetsPerPriority()) {
      // Copy the hosts since running the callbacks replaces the hosts of the host set.
      const HostVector hosts = host_set->hosts();
      for (const HostSharedPtr& host : hosts) {
        passActiveHealthCheck(host);
      }
    }
  }

  // Expects the creation of the timer used to remove the hosts retained with
  // PENDING_DYNAMIC_REMOVAL, which is created the first time it is needed.
  Event::MockTimer* expectGracefulHostRemovalTimer() {
    return new NiceMock<Event::MockTimer>(&server_context_.dispatcher_);
  }

  uint64_t updateNoRebuildCount() const {
    return cluster_->info()->configUpdateStats().update_no_rebuild_.value();
  }

  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment_;
  std::shared_ptr<MockHealthChecker> health_checker_;
};

// Validate that graceful_host_removal implies ignore_health_on_host_removal, and implies
// ignore_new_hosts_until_first_hc only when active health checking is configured.
TEST_F(EdsGracefulHostRemovalTest, ImpliesIgnoreHealthOnHostRemovalAndWarmHosts) {
  EXPECT_TRUE(cluster_->info()->gracefulHostRemoval());
  EXPECT_TRUE(cluster_->info()->drainConnectionsOnHostRemoval());
  EXPECT_TRUE(cluster_->info()->warmHosts());

  resetCluster(R"EOF(
      name: name
      connect_timeout: 0.25s
      type: EDS
      lb_policy: ROUND_ROBIN
      eds_cluster_config:
        service_name: fare
        graceful_host_removal: true
        eds_config:
          api_config_source:
            api_type: REST
            cluster_names:
            - eds
            refresh_delay: 1s
  )EOF",
               Cluster::InitializePhase::Secondary);
  EXPECT_TRUE(cluster_->info()->gracefulHostRemoval());
  EXPECT_TRUE(cluster_->info()->drainConnectionsOnHostRemoval());
  EXPECT_FALSE(cluster_->info()->warmHosts());

  resetCluster();
  EXPECT_FALSE(cluster_->info()->gracefulHostRemoval());
  EXPECT_FALSE(cluster_->info()->drainConnectionsOnHostRemoval());
  EXPECT_FALSE(cluster_->info()->warmHosts());
}

// Validate that when healthy endpoints are replaced by new endpoints awaiting their initial active
// health check, the removed hosts are retained with PENDING_DYNAMIC_REMOVAL until a new host is
// healthy.
TEST_F(EdsGracefulHostRemovalTest, RemovedHostsRetainedUntilNewHostIsHealthy) {
  initializeCluster();
  addEndpoint(80);
  addEndpoint(81);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();
  EXPECT_TRUE(initialized_);
  EXPECT_EQ(2UL, hostSet(0).healthyHosts().size());

  // Replace both endpoints with a new endpoint, which is excluded until its initial active health
  // check completes.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(82);
  updateEndpoints();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:82", "1.2.3.4:80", "1.2.3.4:81"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80", "1.2.3.4:81"));
  const HostSharedPtr new_host = findHost("1.2.3.4:82");
  EXPECT_TRUE(new_host->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC));
  EXPECT_EQ(2UL, hostSet(0).healthyHosts().size());
  ASSERT_EQ(1UL, hostSet(0).excludedHosts().size());
  EXPECT_EQ(new_host, hostSet(0).excludedHosts()[0]);

  // A subsequent update while the new host is warming up keeps the removed hosts retained.
  updateEndpoints();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:82", "1.2.3.4:80", "1.2.3.4:81"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80", "1.2.3.4:81"));
  EXPECT_EQ(new_host, hostSet(0).hosts()[0]);

  // Once the new host passes its initial active health check, it is healthy right away, and the
  // retained hosts are removed on the next event loop iteration.
  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  passActiveHealthCheck(new_host);
  EXPECT_TRUE(timer->enabled());
  EXPECT_EQ(3UL, hostSet(0).hosts().size());
  EXPECT_EQ(3UL, hostSet(0).healthyHosts().size());

  timer->invokeCallback();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:82"));
  EXPECT_EQ(1UL, hostSet(0).healthyHosts().size());
  ASSERT_EQ(1UL, hostSet(0).hostsPerLocality().get().size());
  EXPECT_EQ(1UL, hostSet(0).hostsPerLocality().get()[0].size());
}

// Validate that when replacing multiple endpoints, removed hosts remain retained until the healthy
// panic threshold (default 50%) of configured non-excluded endpoints in the priority can serve
// traffic, and are then dropped even while remaining endpoints are still warming up.
TEST_F(EdsGracefulHostRemovalTest, RemovedHostsRetainedUntilHealthyPanicThresholdReached) {
  initializeCluster();
  addEndpoint(80);
  addEndpoint(85);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  // Replace the endpoints with four new endpoints.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(81);
  addEndpoint(82);
  addEndpoint(83);
  addEndpoint(84);
  updateEndpoints();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:81", "1.2.3.4:82", "1.2.3.4:83",
                                                     "1.2.3.4:84", "1.2.3.4:80", "1.2.3.4:85"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80", "1.2.3.4:85"));
  const HostSharedPtr host_81 = findHost("1.2.3.4:81");
  const HostSharedPtr host_82 = findHost("1.2.3.4:82");

  // 81 fails its initial active health check while 82..84 are still warming up (0/4 = 0% < 50%):
  // retained hosts stay.
  failInitialActiveHealthCheck(host_81);
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80", "1.2.3.4:85"));
  EXPECT_EQ(2UL, hostSet(0).healthyHosts().size());

  // 81 then passes active health checking while 82..84 are still warming up (1/4 = 25% < 50%):
  // 81 joins healthyHosts() alongside the retained hosts rather than dropping all retained capacity
  // onto 81 below the panic threshold.
  passActiveHealthCheck(host_81);
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80", "1.2.3.4:85"));
  EXPECT_EQ(3UL, hostSet(0).healthyHosts().size());

  // Once 82 also passes active health checking (2/4 = 50% >= 50%), all retained hosts are removed
  // even though 83 and 84 are still warming up.
  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  passActiveHealthCheck(host_82);
  timer->invokeCallback();
  EXPECT_THAT(hostAddresses(0),
              testing::ElementsAre("1.2.3.4:81", "1.2.3.4:82", "1.2.3.4:83", "1.2.3.4:84"));
  EXPECT_THAT(retainedHostAddresses(0), testing::IsEmpty());
  EXPECT_EQ(2UL, hostSet(0).healthyHosts().size());
  EXPECT_EQ(2UL, hostSet(0).excludedHosts().size());
}

// Validate that once a new host in another priority is healthy, the retained hosts are removed in a
// single batch update.
TEST_F(EdsGracefulHostRemovalTest, RetainedHostsRemovedAcrossPrioritiesInSingleBatchUpdate) {
  initializeCluster();
  addEndpoint(80, 0);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  // Replace the endpoint of priority 0 with a new endpoint in priority 1.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(81, 1);
  updateEndpoints();
  ASSERT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority().size());
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));
  EXPECT_EQ(1UL, hostSet(0).healthyHosts().size());
  EXPECT_THAT(hostAddresses(1), testing::ElementsAre("1.2.3.4:81"));
  EXPECT_TRUE(hostSet(1).healthyHosts().empty());

  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  passActiveHealthCheck(findHost("1.2.3.4:81"));
  EXPECT_EQ(2UL, cluster_->info()->endpointStats().membership_healthy_.value());

  auto priority_update_cb = cluster_->prioritySet().addPriorityUpdateCb(
      [&](uint32_t, const HostVector&, const HostVector&) {
        EXPECT_TRUE(cluster_->prioritySet().batchUpdateActive());
        return absl::OkStatus();
      });
  uint32_t member_update_calls = 0;
  auto member_update_cb = cluster_->prioritySet().addMemberUpdateCb(
      [&](const HostVector& hosts_added, const HostVector& hosts_removed) {
        ++member_update_calls;
        EXPECT_TRUE(hosts_added.empty());
        EXPECT_EQ(1UL, hosts_removed.size());
        return absl::OkStatus();
      });
  timer->invokeCallback();
  EXPECT_EQ(1U, member_update_calls);
  EXPECT_TRUE(hostSet(0).hosts().empty());
  EXPECT_THAT(hostAddresses(1), testing::ElementsAre("1.2.3.4:81"));
  EXPECT_EQ(1UL, hostSet(1).healthyHosts().size());
}

// Validate that a retained host that fails active health checking is removed, while the other
// retained hosts are kept.
TEST_F(EdsGracefulHostRemovalTest, RetainedHostFailingActiveHealthCheckRemoved) {
  initializeCluster();
  addEndpoint(80);
  addEndpoint(81);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  cluster_load_assignment_.clear_endpoints();
  addEndpoint(82);
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80", "1.2.3.4:81"));

  // 80 fails active health checking while 82 is warming up: it is excluded from the healthy hosts
  // right away, and removed on the next event loop iteration.
  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  const HostSharedPtr host_80 = findHost("1.2.3.4:80");
  host_80->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker_->runCallbacks(host_80, HealthTransition::Changed, HealthState::Unhealthy);
  EXPECT_EQ(3UL, hostSet(0).hosts().size());
  EXPECT_EQ(1UL, hostSet(0).healthyHosts().size());

  timer->invokeCallback();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:82", "1.2.3.4:81"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:81"));
}

// Validate that a retained host that becomes degraded (via active health checking or outlier
// detection) is moved out of healthyHosts() right away and removed on the next event loop
// iteration, while other healthy retained hosts are kept.
TEST_F(EdsGracefulHostRemovalTest, RetainedHostBecomingDegradedRemoved) {
  initializeCluster();
  auto outlier_detector = std::make_shared<NiceMock<Outlier::MockDetector>>();
  cluster_->setOutlierDetector(outlier_detector);
  addEndpoint(80);
  addEndpoint(81);
  addEndpoint(82);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  cluster_load_assignment_.clear_endpoints();
  addEndpoint(83);
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0),
              testing::ElementsAre("1.2.3.4:80", "1.2.3.4:81", "1.2.3.4:82"));

  // 80 becomes degraded via active health checking while 83 is warming up: it moves out of
  // healthyHosts() immediately and is removed on the next event loop iteration.
  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  const HostSharedPtr host_80 = findHost("1.2.3.4:80");
  host_80->healthFlagSet(Host::HealthFlag::DEGRADED_ACTIVE_HC);
  health_checker_->runCallbacks(host_80, HealthTransition::Changed, HealthState::Healthy);
  EXPECT_EQ(4UL, hostSet(0).hosts().size());
  EXPECT_EQ(2UL, hostSet(0).healthyHosts().size());
  EXPECT_EQ(1UL, hostSet(0).degradedHosts().size());
  EXPECT_TRUE(timer->enabled());

  timer->invokeCallback();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:83", "1.2.3.4:81", "1.2.3.4:82"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:81", "1.2.3.4:82"));
  EXPECT_TRUE(hostSet(0).degradedHosts().empty());

  // 81 becomes degraded via outlier detection: it is also removed on the next event loop iteration.
  const HostSharedPtr host_81 = findHost("1.2.3.4:81");
  host_81->healthFlagSet(Host::HealthFlag::DEGRADED_OUTLIER_DETECTION);
  outlier_detector->runCallbacks(host_81);
  EXPECT_EQ(3UL, hostSet(0).hosts().size());
  EXPECT_EQ(1UL, hostSet(0).healthyHosts().size());
  EXPECT_EQ(1UL, hostSet(0).degradedHosts().size());
  EXPECT_TRUE(timer->enabled());

  timer->invokeCallback();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:83", "1.2.3.4:82"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:82"));
  EXPECT_TRUE(hostSet(0).degradedHosts().empty());
}

// Validate that a retained host that is ejected by outlier detection is excluded from the healthy
// hosts right away, and removed along with its locality on the next event loop iteration, rather
// than within the outlier detector callback.
TEST_F(EdsGracefulHostRemovalTest, RetainedHostEjectedByOutlierDetectionRemoved) {
  initializeCluster();
  auto outlier_detector = std::make_shared<NiceMock<Outlier::MockDetector>>();
  cluster_->setOutlierDetector(outlier_detector);
  addEndpoint(80, 0, 0, "a", 100);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();
  const HostSharedPtr old_host = findHost("1.2.3.4:80");

  cluster_load_assignment_.clear_endpoints();
  addEndpoint(81, 0, 0, "b", 50);
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));
  EXPECT_EQ(1UL, hostSet(0).healthyHosts().size());

  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  old_host->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector->runCallbacks(old_host);
  EXPECT_EQ(2UL, hostSet(0).hosts().size());
  EXPECT_TRUE(hostSet(0).healthyHosts().empty());
  EXPECT_TRUE(timer->enabled());

  timer->invokeCallback();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:81"));
  ASSERT_EQ(1UL, hostSet(0).hostsPerLocality().get().size());
  EXPECT_EQ("b", hostSet(0).hostsPerLocality().get()[0][0]->locality().region());
  ASSERT_NE(nullptr, hostSet(0).localityWeights());
  EXPECT_THAT(*hostSet(0).localityWeights(), testing::ElementsAre(50));
}

// Validate that when the only host warming up is ejected by outlier detection, the retained hosts
// are removed on the next event loop iteration rather than within the outlier detector callback.
TEST_F(EdsGracefulHostRemovalTest, RetainedHostsRemovedWhenWarmingHostEjectedByOutlierDetection) {
  initializeCluster();
  auto outlier_detector = std::make_shared<NiceMock<Outlier::MockDetector>>();
  cluster_->setOutlierDetector(outlier_detector);
  addEndpoint(80);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  cluster_load_assignment_.clear_endpoints();
  addEndpoint(81);
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));

  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  const HostSharedPtr new_host = findHost("1.2.3.4:81");
  new_host->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector->runCallbacks(new_host);
  EXPECT_EQ(2UL, hostSet(0).hosts().size());
  EXPECT_TRUE(timer->enabled());

  timer->invokeCallback();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:81"));
}

// Validate that when the healthy hosts are reloaded while a batch update is active, the priorities
// are not updated until the next event loop iteration, which then also removes the retained hosts.
TEST_F(EdsGracefulHostRemovalTest, ReloadDuringBatchUpdateDeferred) {
  initializeCluster();
  addEndpoint(80);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  cluster_load_assignment_.clear_endpoints();
  addEndpoint(81);
  updateEndpoints();
  const HostSharedPtr new_host = findHost("1.2.3.4:81");

  uint32_t priority_update_calls = 0;
  auto priority_update_cb = cluster_->prioritySet().addPriorityUpdateCb(
      [&](uint32_t, const HostVector&, const HostVector&) {
        ++priority_update_calls;
        return absl::OkStatus();
      });
  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  FunctionBatchUpdate batch_update([&]() { passActiveHealthCheck(new_host); });
  cluster_->prioritySet().batchHostUpdate(batch_update);
  EXPECT_EQ(0U, priority_update_calls);
  EXPECT_TRUE(timer->enabled());
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));

  timer->invokeCallback();
  EXPECT_EQ(1U, priority_update_calls);
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:81"));
  EXPECT_EQ(1UL, hostSet(0).healthyHosts().size());
}

// Validate that the retained hosts are removed once no host is warming up anymore, even if no host
// is healthy.
TEST_F(EdsGracefulHostRemovalTest, RetainedHostsRemovedWhenWarmupEndsWithoutHealthyHost) {
  initializeCluster();
  addEndpoint(80);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  cluster_load_assignment_.clear_endpoints();
  addEndpoint(81);
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));

  // The new host passes its initial active health check, but is degraded by it.
  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  const HostSharedPtr new_host = findHost("1.2.3.4:81");
  new_host->healthFlagSet(Host::HealthFlag::DEGRADED_ACTIVE_HC);
  passActiveHealthCheck(new_host);
  timer->invokeCallback();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:81"));
}

// Validate that hosts that previously failed their initial active health check, or that are
// excluded by an immediate health check failure, are not warming up and therefore do not keep
// removed hosts retained.
TEST_F(EdsGracefulHostRemovalTest, HostsNotAwaitingInitialHealthCheckAreNotWarmingUp) {
  initializeCluster();
  addEndpoint(80);
  addEndpoint(81);
  updateEndpoints();
  passActiveHealthCheck(findHost("1.2.3.4:80"));
  failInitialActiveHealthCheck(findHost("1.2.3.4:81"));

  // Remove 80 while keeping 81, which already failed its initial active health check.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(81);
  updateEndpoints();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:81"));

  // Add 80 back, and replace it with 82 once it is healthy.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(80);
  updateEndpoints();
  passActiveHealthCheck(findHost("1.2.3.4:80"));
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(82);
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));

  // 82 is excluded by an immediate health check failure before completing its initial active
  // health check.
  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  const HostSharedPtr host_82 = findHost("1.2.3.4:82");
  host_82->healthFlagSet(Host::HealthFlag::EXCLUDED_VIA_IMMEDIATE_HC_FAIL);
  health_checker_->runCallbacks(host_82, HealthTransition::Changed, HealthState::Unhealthy);
  timer->invokeCallback();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:82"));
}

// Validate that removed hosts that are not strictly healthy (including degraded hosts), that are
// excluded from load balancing, or that are not actively health checked, are not retained.
TEST_F(EdsGracefulHostRemovalTest, RemovedHostsThatAreNotHealthyNotRetained) {
  initializeCluster();
  addEndpoint(80);
  addEndpoint(81, 0, 0, "", 0, "", envoy::config::core::v3::DRAINING);
  addEndpoint(82);
  addEndpoint(83);
  addEndpoint(84);
  addEndpoint(86, 0, 0, "", 0, "", envoy::config::core::v3::UNHEALTHY);
  addEndpoint(87);
  cluster_load_assignment_.mutable_endpoints(6)
      ->mutable_lb_endpoints(0)
      ->mutable_endpoint()
      ->mutable_health_check_config()
      ->set_disable_active_health_check(true);
  addEndpoint(88, 0, 0, "", 0, "", envoy::config::core::v3::DEGRADED);
  addEndpoint(89);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();
  // 82 is ejected by outlier detection, 83 is degraded by active health checking, 84 fails active
  // health checking, 86 is marked unhealthy by EDS, 87 is healthy but not actively health checked,
  // 88 is marked degraded by EDS, and 89 is degraded by outlier detection.
  findHost("1.2.3.4:82")->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  findHost("1.2.3.4:83")->healthFlagSet(Host::HealthFlag::DEGRADED_ACTIVE_HC);
  findHost("1.2.3.4:84")->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_TRUE(findHost("1.2.3.4:86")->healthFlagGet(Host::HealthFlag::FAILED_EDS_HEALTH));
  EXPECT_EQ(Host::Health::Healthy, findHost("1.2.3.4:87")->coarseHealth());
  EXPECT_TRUE(findHost("1.2.3.4:88")->healthFlagGet(Host::HealthFlag::DEGRADED_EDS_HEALTH));
  findHost("1.2.3.4:89")->healthFlagSet(Host::HealthFlag::DEGRADED_OUTLIER_DETECTION);

  // Replace all the endpoints with 85: only the strictly healthy actively health checked host 80 is
  // retained.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(85);
  updateEndpoints();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:85", "1.2.3.4:80"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));
  EXPECT_EQ(1UL, hostSet(0).healthyHosts().size());
  EXPECT_TRUE(hostSet(0).degradedHosts().empty());
}

// Validate that when active health checking is enabled for an endpoint that had it disabled, its
// host is replaced by a new host awaiting its initial active health check, during which the other
// removed hosts are retained.
TEST_F(EdsGracefulHostRemovalTest, ActiveHealthCheckEnabledForEndpointRetainsRemovedHosts) {
  initializeCluster();
  addEndpoint(80);
  addEndpoint(81);
  cluster_load_assignment_.mutable_endpoints(0)
      ->mutable_lb_endpoints(0)
      ->mutable_endpoint()
      ->mutable_health_check_config()
      ->set_disable_active_health_check(true);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();
  const HostSharedPtr old_host_80 = findHost("1.2.3.4:80");
  EXPECT_TRUE(old_host_80->disableActiveHealthCheck());
  EXPECT_EQ(2UL, hostSet(0).healthyHosts().size());

  // Enable active health checking for 80, and remove 81.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(80);
  updateEndpoints();
  const HostSharedPtr new_host_80 = findHost("1.2.3.4:80");
  EXPECT_NE(old_host_80, new_host_80);
  EXPECT_FALSE(new_host_80->disableActiveHealthCheck());
  EXPECT_TRUE(new_host_80->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC));
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:80", "1.2.3.4:81"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:81"));
  EXPECT_EQ(1UL, hostSet(0).healthyHosts().size());

  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  passActiveHealthCheck(new_host_80);
  timer->invokeCallback();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:80"));
  EXPECT_EQ(1UL, hostSet(0).healthyHosts().size());
}

// Validate that when healthy endpoints are replaced by DEGRADED endpoints awaiting their initial
// active health check, the removed hosts are retained until the DEGRADED endpoints complete their
// initial active health check and become able to serve traffic as degraded hosts.
TEST_F(EdsGracefulHostRemovalTest,
       RemovedHostsRetainedUntilDegradedEndpointPassesInitialHealthCheck) {
  initializeCluster();
  addEndpoint(80);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();
  const HostSharedPtr old_host = findHost("1.2.3.4:80");

  cluster_load_assignment_.clear_endpoints();
  addEndpoint(81, 0, 0, "", 0, "", envoy::config::core::v3::DEGRADED);
  updateEndpoints();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:81", "1.2.3.4:80"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));
  const HostSharedPtr new_host = findHost("1.2.3.4:81");
  ASSERT_EQ(1UL, hostSet(0).healthyHosts().size());
  EXPECT_EQ(old_host, hostSet(0).healthyHosts()[0]);
  EXPECT_TRUE(hostSet(0).degradedHosts().empty());
  ASSERT_EQ(1UL, hostSet(0).excludedHosts().size());
  EXPECT_EQ(new_host, hostSet(0).excludedHosts()[0]);

  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  passActiveHealthCheck(new_host);
  timer->invokeCallback();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:81"));
  EXPECT_TRUE(hostSet(0).healthyHosts().empty());
  ASSERT_EQ(1UL, hostSet(0).degradedHosts().size());
  EXPECT_EQ(new_host, hostSet(0).degradedHosts()[0]);
}

// Validate that degraded advertised endpoints do not count toward reaching healthy_panic_threshold
// (either within a priority or across priorities), so retained healthy hosts stay retained while
// other endpoints are still warming up until strictly healthy capacity reaches the threshold.
TEST_F(EdsGracefulHostRemovalTest, DegradedAdvertisedHostsDoNotCountTowardHealthyPanicThreshold) {
  initializeCluster();
  addEndpoint(80, 0);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  // Replace 80 with 4 warming endpoints in priority 0: 81 (DEGRADED in EDS) and 82..84 (HEALTHY).
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(81, 0, 0, "", 0, "", envoy::config::core::v3::DEGRADED);
  addEndpoint(82, 0);
  addEndpoint(83, 0);
  addEndpoint(84, 0);
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));

  // 81 (degraded) and 82 (healthy) pass active HC while 83 and 84 are still warming up: 2/4
  // endpoints can serve traffic, but only 1/4 = 25% < 50% are strictly healthy, so 80 stays
  // retained.
  passActiveHealthCheck(findHost("1.2.3.4:81"));
  passActiveHealthCheck(findHost("1.2.3.4:82"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));
  EXPECT_EQ(2UL, hostSet(0).healthyHosts().size());
  EXPECT_EQ(1UL, hostSet(0).degradedHosts().size());

  // Once 83 (healthy) also passes active HC (2/4 = 50% >= 50% strictly healthy), 80 is removed
  // while 84 continues warming up.
  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  passActiveHealthCheck(findHost("1.2.3.4:83"));
  timer->invokeCallback();
  EXPECT_THAT(retainedHostAddresses(0), testing::IsEmpty());
  EXPECT_THAT(hostAddresses(0),
              testing::ElementsAre("1.2.3.4:81", "1.2.3.4:82", "1.2.3.4:83", "1.2.3.4:84"));
  passActiveHealthCheck(findHost("1.2.3.4:84"));

  // Also validate cross-priority evaluation: empty priority 0 while priority 1 has 1 degraded host
  // (91) and 1 warming healthy host (92). Because priority 1 has 0/2 < 50% strictly healthy hosts,
  // priority 0 retains 82..84 until 92 becomes healthy.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(91, 1, 0, "", 0, "", envoy::config::core::v3::DEGRADED);
  addEndpoint(92, 1);
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0),
              testing::ElementsAre("1.2.3.4:82", "1.2.3.4:83", "1.2.3.4:84"));

  passActiveHealthCheck(findHost("1.2.3.4:91"));
  EXPECT_THAT(retainedHostAddresses(0),
              testing::ElementsAre("1.2.3.4:82", "1.2.3.4:83", "1.2.3.4:84"));

  passActiveHealthCheck(findHost("1.2.3.4:92"));
  EXPECT_TRUE(timer->enabled());
  timer->invokeCallback();
  EXPECT_TRUE(hostSet(0).hosts().empty());
  EXPECT_EQ(1UL, hostSet(1).healthyHosts().size());
  EXPECT_EQ(1UL, hostSet(1).degradedHosts().size());
}

// Validate that removed hosts are not retained when no host in the updated configuration is
// warming up, i.e. when no host is expected to become able to serve traffic (healthy or degraded)
// by passing its initial active health check.
class EdsGracefulHostRemovalNoWarmingHostTest
    : public EdsGracefulHostRemovalTest,
      public testing::WithParamInterface<envoy::config::core::v3::HealthStatus> {};

INSTANTIATE_TEST_SUITE_P(
    HealthStatus, EdsGracefulHostRemovalNoWarmingHostTest,
    testing::Values(envoy::config::core::v3::UNHEALTHY, envoy::config::core::v3::DRAINING),
    [](const testing::TestParamInfo<envoy::config::core::v3::HealthStatus>& info) {
      return envoy::config::core::v3::HealthStatus_Name(info.param);
    });

TEST_P(EdsGracefulHostRemovalNoWarmingHostTest, RemovedHostNotRetained) {
  initializeCluster();
  addEndpoint(80);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  // Replace 80 with 81, which the update marks as not healthy.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(81, 0, 0, "", 0, "", GetParam());
  updateEndpoints();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:81"));
}

// Validate that without active health checking, removed hosts are not retained.
TEST_F(EdsGracefulHostRemovalTest, RemovedHostsNotRetainedWithoutActiveHealthChecking) {
  initializeCluster(/*with_health_checker=*/false);
  addEndpoint(80);
  updateEndpoints();

  cluster_load_assignment_.clear_endpoints();
  addEndpoint(81);
  updateEndpoints();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:81"));
  EXPECT_EQ(1UL, hostSet(0).healthyHosts().size());
}

// Validate that when a new endpoint has active health checking disabled, it is healthy right away,
// so the removed hosts are not retained.
TEST_F(EdsGracefulHostRemovalTest, RemovedHostsNotRetainedWhenNewHostHasActiveHealthCheckDisabled) {
  initializeCluster();
  addEndpoint(80);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  cluster_load_assignment_.clear_endpoints();
  addEndpoint(81);
  cluster_load_assignment_.mutable_endpoints(0)
      ->mutable_lb_endpoints(0)
      ->mutable_endpoint()
      ->mutable_health_check_config()
      ->set_disable_active_health_check(true);
  updateEndpoints();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:81"));
  EXPECT_EQ(1UL, hostSet(0).healthyHosts().size());
}

// Validate that the retained hosts of a priority without warming hosts of its own are removed by an
// update that makes a host in another priority healthy.
TEST_F(EdsGracefulHostRemovalTest, RetainedHostsRemovedWhenUpdateMarksHostHealthy) {
  initializeCluster();
  addEndpoint(80, 0);
  addEndpoint(81, 1);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  // Remove 80 from priority 0, add warming 82 to priority 1, and mark 81 UNHEALTHY in priority 1:
  // priority 0 retains 80 while priority 1 is warming up and no advertised host can serve traffic.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(82, 1);
  addEndpoint(81, 1, 0, "", 0, "", envoy::config::core::v3::UNHEALTHY);
  updateEndpoints();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:80"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));
  EXPECT_TRUE(hostSet(1).healthyHosts().empty());

  // Mark 81 HEALTHY in priority 1 while 82 is still warming up: priority 0 removes its retained
  // host since the cluster now has a healthy advertised host.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(82, 1);
  addEndpoint(81, 1, 0, "", 0, "", envoy::config::core::v3::HEALTHY);
  updateEndpoints();
  EXPECT_TRUE(hostSet(0).hosts().empty());
  EXPECT_EQ(1UL, hostSet(1).healthyHosts().size());
}

// Validate that in a multi-priority fallback cluster, rotating endpoints in priority 0 retains
// priority 0's removed hosts during warmup until priority 0 reaches the healthy panic threshold
// even when priority 1 has a healthy fallback host (rather than spilling traffic to priority 1),
// and rotating priority 1's fallback endpoint retains priority 1's previous host during warmup even
// when priority 0 is healthy.
TEST_F(EdsGracefulHostRemovalTest, MultiPriorityFallbackRetainsPerPriorityIndependently) {
  initializeCluster();
  addEndpoint(80, 0);
  addEndpoint(81, 0);
  addEndpoint(90, 1);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  // Replace priority 0's primary endpoints with warming 82, 83, and 84 while priority 1's fallback
  // 90 stays healthy: priority 0 retains 80 and 81 until priority 0 reaches the 50% threshold.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(82, 0);
  addEndpoint(83, 0);
  addEndpoint(84, 0);
  addEndpoint(90, 1);
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80", "1.2.3.4:81"));
  EXPECT_EQ(2UL, hostSet(0).healthyHosts().size());
  EXPECT_EQ(1UL, hostSet(1).healthyHosts().size());

  // 82 passes HC (1/3 = 33% < 50%): 80 and 81 stay retained in priority 0.
  passActiveHealthCheck(findHost("1.2.3.4:82"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80", "1.2.3.4:81"));
  EXPECT_EQ(3UL, hostSet(0).healthyHosts().size());

  // 83 passes HC (2/3 = 66% >= 50%): 80 and 81 are removed from priority 0 while 84 is still
  // warming up.
  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  passActiveHealthCheck(findHost("1.2.3.4:83"));
  timer->invokeCallback();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:82", "1.2.3.4:83", "1.2.3.4:84"));
  EXPECT_EQ(2UL, hostSet(0).healthyHosts().size());
  passActiveHealthCheck(findHost("1.2.3.4:84"));

  // Now replace priority 1's fallback 90 with warming 91 while priority 0 stays healthy: priority 1
  // retains 90 until 91 finishes warming up.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(82, 0);
  addEndpoint(83, 0);
  addEndpoint(84, 0);
  addEndpoint(91, 1);
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(1), testing::ElementsAre("1.2.3.4:90"));
  EXPECT_EQ(1UL, hostSet(1).healthyHosts().size());

  passActiveHealthCheck(findHost("1.2.3.4:91"));
  EXPECT_TRUE(timer->enabled());
  timer->invokeCallback();
  EXPECT_THAT(hostAddresses(1), testing::ElementsAre("1.2.3.4:91"));
  EXPECT_EQ(1UL, hostSet(1).healthyHosts().size());
}

// Validate that when a subset of endpoints in a priority is replaced and the surviving healthy
// endpoints are below the healthy panic threshold, the removed hosts stay retained until the
// priority reaches the healthy panic threshold.
TEST_F(EdsGracefulHostRemovalTest, PartialEndpointReplacementRetainsRemovedHostsDuringWarmup) {
  initializeCluster();
  addEndpoint(80);
  addEndpoint(81);
  addEndpoint(82);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  // Keep healthy 80 and replace 81 and 82 with warming 83..86 (1/5 = 20% < 50%).
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(80);
  addEndpoint(83);
  addEndpoint(84);
  addEndpoint(85);
  addEndpoint(86);
  updateEndpoints();
  EXPECT_THAT(hostAddresses(0),
              testing::ElementsAre("1.2.3.4:80", "1.2.3.4:83", "1.2.3.4:84", "1.2.3.4:85",
                                   "1.2.3.4:86", "1.2.3.4:81", "1.2.3.4:82"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:81", "1.2.3.4:82"));
  EXPECT_EQ(3UL, hostSet(0).healthyHosts().size());

  // 83 passes HC (2/5 = 40% < 50%): 81 and 82 stay retained.
  passActiveHealthCheck(findHost("1.2.3.4:83"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:81", "1.2.3.4:82"));
  EXPECT_EQ(4UL, hostSet(0).healthyHosts().size());

  // 84 passes HC (3/5 = 60% >= 50%): 81 and 82 are removed while 85 and 86 continue warming up.
  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  passActiveHealthCheck(findHost("1.2.3.4:84"));
  timer->invokeCallback();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:80", "1.2.3.4:83", "1.2.3.4:84",
                                                     "1.2.3.4:85", "1.2.3.4:86"));
  EXPECT_THAT(retainedHostAddresses(0), testing::IsEmpty());
  EXPECT_EQ(3UL, hostSet(0).healthyHosts().size());
}

// Validate that a host moved to another priority is reused.
TEST_F(EdsGracefulHostRemovalTest, HostMovedToAnotherPriorityReused) {
  initializeCluster();
  addEndpoint(80, 0);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();
  const HostSharedPtr host_80 = findHost("1.2.3.4:80");

  // Move 80 to priority 1, and add 81 to priority 0.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(81, 0);
  addEndpoint(80, 1);
  updateEndpoints();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:81"));
  ASSERT_EQ(1UL, hostSet(1).hosts().size());
  EXPECT_EQ(host_80, hostSet(1).hosts()[0]);
  EXPECT_EQ(host_80, findHost("1.2.3.4:80"));
  EXPECT_EQ(1UL, hostSet(1).healthyHosts().size());
}

// Validate that when the health check address of an endpoint changes, its host is replaced right
// away by a new host awaiting its initial active health check (as with
// ignore_health_on_host_removal), while the other removed hosts are retained until the new host is
// healthy.
TEST_F(EdsGracefulHostRemovalTest, HealthCheckAddressChangeReplacesHostImmediately) {
  initializeCluster();
  addEndpoint(80, 0, 8000);
  addEndpoint(81, 0, 8001);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();
  const HostSharedPtr old_host_80 = findHost("1.2.3.4:80");

  // Change the health check port of 80, and remove 81.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(80, 0, 9000);
  updateEndpoints();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:80", "1.2.3.4:81"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:81"));
  const HostSharedPtr new_host_80 = hostSet(0).hosts()[0];
  EXPECT_NE(old_host_80, new_host_80);
  EXPECT_EQ(9000U, new_host_80->healthCheckAddress()->ip()->port());
  EXPECT_TRUE(new_host_80->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC));
  EXPECT_EQ(new_host_80, findHost("1.2.3.4:80"));
  EXPECT_EQ(1UL, hostSet(0).healthyHosts().size());

  // A subsequent update while the new host is warming up reuses it.
  updateEndpoints();
  EXPECT_EQ(new_host_80, hostSet(0).hosts()[0]);
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:81"));

  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  passActiveHealthCheck(new_host_80);
  timer->invokeCallback();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:80"));
  EXPECT_EQ(new_host_80, findHost("1.2.3.4:80"));
  EXPECT_EQ(1UL, hostSet(0).healthyHosts().size());
}

// Validate that when the locality of an endpoint changes, its host is replaced right away by a new
// host, which keeps the active health check status of the replaced host and is therefore healthy,
// so that the other removed hosts are not retained either.
TEST_F(EdsGracefulHostRemovalTest, LocalityChangeReplacesHostImmediately) {
  initializeCluster();
  addEndpoint(80, 0, 0, "a");
  addEndpoint(81, 0, 0, "a");
  updateEndpoints();
  passActiveHealthCheckForAllHosts();
  const HostSharedPtr old_host_80 = findHost("1.2.3.4:80");

  // Move 80 to locality b, and remove 81.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(80, 0, 0, "b");
  updateEndpoints();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:80"));
  const HostSharedPtr new_host_80 = hostSet(0).hosts()[0];
  EXPECT_NE(old_host_80, new_host_80);
  EXPECT_EQ("b", new_host_80->locality().region());
  EXPECT_EQ(new_host_80, findHost("1.2.3.4:80"));
  EXPECT_EQ(1UL, hostSet(0).healthyHosts().size());
}

// Validate that when the locality and the health check address of an endpoint change, its host is
// replaced right away, while the other removed hosts are retained until the new host is healthy.
TEST_F(EdsGracefulHostRemovalTest, LocalityAndHealthCheckAddressChangeReplacesHostImmediately) {
  initializeCluster();
  addEndpoint(80, 0, 8000, "a");
  addEndpoint(81, 0, 8001, "a");
  updateEndpoints();
  passActiveHealthCheckForAllHosts();
  const HostSharedPtr old_host_80 = findHost("1.2.3.4:80");

  // Move 80 to locality b with a new health check port, and remove 81.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(80, 0, 9000, "b");
  updateEndpoints();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:80", "1.2.3.4:81"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:81"));
  const HostSharedPtr new_host_80 = findHost("1.2.3.4:80");
  EXPECT_NE(old_host_80, new_host_80);
  EXPECT_EQ("b", new_host_80->locality().region());
  const auto& hosts_per_locality = hostSet(0).hostsPerLocality().get();
  ASSERT_EQ(2UL, hosts_per_locality.size());
  EXPECT_EQ("a", hosts_per_locality[0][0]->locality().region());
  EXPECT_EQ("b", hosts_per_locality[1][0]->locality().region());

  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  passActiveHealthCheck(new_host_80);
  timer->invokeCallback();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:80"));
  ASSERT_EQ(1UL, hostSet(0).hostsPerLocality().get().size());

  // A subsequent identical update does not rebuild the cluster.
  const uint64_t update_no_rebuild = updateNoRebuildCount();
  updateEndpoints();
  EXPECT_EQ(update_no_rebuild + 1, updateNoRebuildCount());
}

// Validate that when the hostname of an endpoint changes, its host is replaced right away by a new
// host, which keeps the active health check status of the replaced host, so that the other removed
// hosts are not retained even if no host is healthy after the update.
TEST_F(EdsGracefulHostRemovalTest, HostnameChangeReplacesHostImmediately) {
  initializeCluster();
  addEndpoint(80, 0, 0, "", 0, "foo");
  addEndpoint(81);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();
  const HostSharedPtr old_host_80 = findHost("1.2.3.4:80");

  // Change the hostname of 80 and mark it DEGRADED, and remove 81.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(80, 0, 0, "", 0, "bar", envoy::config::core::v3::DEGRADED);
  updateEndpoints();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:80"));
  const HostSharedPtr new_host_80 = hostSet(0).hosts()[0];
  EXPECT_NE(old_host_80, new_host_80);
  EXPECT_EQ("bar", new_host_80->hostname());
  EXPECT_FALSE(new_host_80->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_EQ(1UL, hostSet(0).degradedHosts().size());
}

// Validate that when the priority and the health check address of an endpoint change, its host is
// replaced right away by a new host in the other priority, which then owns the address in the cross
// priority host map.
TEST_F(EdsGracefulHostRemovalTest, PriorityAndHealthCheckAddressChangeReplacesHostImmediately) {
  initializeCluster();
  addEndpoint(80, 0, 8000);
  addEndpoint(81, 0, 8001);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();
  const HostSharedPtr old_host_80 = findHost("1.2.3.4:80");

  // Move 80 to priority 1 with a new health check port, and remove 81.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(80, 1, 9000);
  updateEndpoints();
  ASSERT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority().size());
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:81"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:81"));
  EXPECT_THAT(hostAddresses(1), testing::ElementsAre("1.2.3.4:80"));
  const HostSharedPtr new_host_80 = hostSet(1).hosts()[0];
  EXPECT_NE(old_host_80, new_host_80);
  EXPECT_EQ(new_host_80, findHost("1.2.3.4:80"));

  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  passActiveHealthCheck(new_host_80);
  timer->invokeCallback();
  EXPECT_TRUE(hostSet(0).hosts().empty());
  EXPECT_EQ(new_host_80, findHost("1.2.3.4:80"));
}

// Validate that when a host (including a retained host) in priority 1 is replaced by a new host
// with the same address in priority 0, the new host in priority 0 owns the address in the cross
// priority host map even though priority 0 updates before priority 1 removes the old host.
TEST_F(EdsGracefulHostRemovalTest, RetainedHostReplacedInHigherPriorityOwnsAddress) {
  initializeCluster();
  addEndpoint(80, 1, 8000);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();
  const HostSharedPtr old_host_80 = findHost("1.2.3.4:80");

  // Replace 80 in priority 1 with warming 81 so 80 is retained with PENDING_DYNAMIC_REMOVAL in
  // priority 1.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(81, 1);
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(1), testing::ElementsAre("1.2.3.4:80"));
  EXPECT_EQ(old_host_80, findHost("1.2.3.4:80"));

  // Re-add 80 in priority 0 with a new health check port while 81 is still warming in priority 1.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(80, 0, 9000);
  addEndpoint(81, 1);
  updateEndpoints();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:80"));
  EXPECT_THAT(hostAddresses(1), testing::ElementsAre("1.2.3.4:81"));
  const HostSharedPtr new_host_80 = hostSet(0).hosts()[0];
  EXPECT_NE(old_host_80, new_host_80);
  EXPECT_EQ(9000U, new_host_80->healthCheckAddress()->ip()->port());
  EXPECT_EQ(new_host_80, findHost("1.2.3.4:80"));

  // Subsequent updates reuse new_host_80 in priority 0 rather than recreating it from scratch.
  updateEndpoints();
  EXPECT_EQ(new_host_80, hostSet(0).hosts()[0]);
  EXPECT_EQ(new_host_80, findHost("1.2.3.4:80"));
}

// Validate that when a retained host is added back, it is reused.
TEST_F(EdsGracefulHostRemovalTest, RetainedHostAddedBackReused) {
  initializeCluster();
  addEndpoint(80);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();
  const HostSharedPtr host_80 = findHost("1.2.3.4:80");

  cluster_load_assignment_.clear_endpoints();
  addEndpoint(82);
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));

  addEndpoint(80);
  updateEndpoints();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:82", "1.2.3.4:80"));
  EXPECT_THAT(retainedHostAddresses(0), testing::IsEmpty());
  EXPECT_EQ(host_80, findHost("1.2.3.4:80"));
  EXPECT_EQ(1UL, hostSet(0).healthyHosts().size());
}

// Validate that when a retained host is added back with a new health check address, it is replaced
// right away by a new host.
TEST_F(EdsGracefulHostRemovalTest, RetainedHostAddedBackWithNewHealthCheckAddressReplaced) {
  initializeCluster();
  addEndpoint(80, 0, 8000);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();
  const HostSharedPtr old_host_80 = findHost("1.2.3.4:80");

  cluster_load_assignment_.clear_endpoints();
  addEndpoint(82);
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));

  addEndpoint(80, 0, 9000);
  updateEndpoints();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:82", "1.2.3.4:80"));
  EXPECT_THAT(retainedHostAddresses(0), testing::IsEmpty());
  const HostSharedPtr new_host_80 = findHost("1.2.3.4:80");
  EXPECT_NE(old_host_80, new_host_80);
  EXPECT_EQ(9000U, new_host_80->healthCheckAddress()->ip()->port());
  EXPECT_TRUE(hostSet(0).healthyHosts().empty());
}

// Validate that the hosts of a priority that is not present in the updated configuration anymore
// are retained while another priority warms up.
TEST_F(EdsGracefulHostRemovalTest, HostsOfPriorityRemovedFromConfigurationRetained) {
  initializeCluster();
  addEndpoint(80, 0);
  addEndpoint(90, 1);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  // Replace 80 with 81 in priority 0, and remove priority 1 from the configuration.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(81, 0);
  updateEndpoints();
  ASSERT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority().size());
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));
  EXPECT_THAT(hostAddresses(1), testing::ElementsAre("1.2.3.4:90"));
  EXPECT_THAT(retainedHostAddresses(1), testing::ElementsAre("1.2.3.4:90"));

  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  passActiveHealthCheck(findHost("1.2.3.4:81"));
  timer->invokeCallback();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:81"));
  EXPECT_TRUE(hostSet(1).hosts().empty());
}

// Validate that with active health checking configured, graceful host removal excludes newly added
// hosts until their first active health check completes, so that they do not count towards the
// panic threshold or priority and locality availability while removed hosts are retained.
TEST_F(EdsGracefulHostRemovalTest, NewHostsExcludedUntilFirstHealthCheck) {
  EXPECT_TRUE(cluster_->info()->warmHosts());
  ThreadAwareLoadBalancerPtr thread_aware_lb = createThreadAwareLoadBalancer();
  initializeCluster();
  addEndpoint(80);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();
  const HostSharedPtr old_host = findHost("1.2.3.4:80");

  // Replace 80 with 4 new hosts. If the new hosts counted towards the panic threshold, only 1 of
  // the 5 hosts (20%) would be healthy, which is below the default panic threshold of 50%.
  cluster_load_assignment_.clear_endpoints();
  for (uint32_t port = 81; port <= 84; ++port) {
    addEndpoint(port);
  }
  updateEndpoints();
  EXPECT_TRUE(old_host->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
  EXPECT_EQ(4UL, hostSet(0).excludedHosts().size());
  for (const HostSharedPtr& new_host : hostSet(0).excludedHosts()) {
    EXPECT_TRUE(new_host->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_TRUE(new_host->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC));
  }
  ASSERT_EQ(1UL, hostSet(0).healthyHosts().size());
  EXPECT_EQ(old_host, hostSet(0).healthyHosts()[0]);

  // Load balancing is not in panic mode, and only selects the retained host.
  LoadBalancerPtr lb = thread_aware_lb->factory()->create({cluster_->prioritySet(), nullptr});
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(old_host, lb->chooseHost(nullptr).host);
  }
  EXPECT_EQ(0UL, cluster_->info()->lbStats().lb_healthy_panic_.value());

  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  for (uint32_t port = 81; port <= 84; ++port) {
    passActiveHealthCheck(findHost("1.2.3.4:" + std::to_string(port)));
  }
  timer->invokeCallback();
  EXPECT_THAT(hostAddresses(0),
              testing::ElementsAre("1.2.3.4:81", "1.2.3.4:82", "1.2.3.4:83", "1.2.3.4:84"));
  EXPECT_TRUE(hostSet(0).excludedHosts().empty());
  EXPECT_EQ(4UL, hostSet(0).healthyHosts().size());
}

// Validate that pruning an empty non-local locality bucket when removing retained hosts preserves
// hasLocalLocality() == true when the local locality bucket (bucket 0) remains populated.
TEST_F(EdsGracefulHostRemovalTest, LocalLocalityPreservedWhenPruningEmptyNonLocalLocality) {
  initializeCluster();
  addEndpointInZone(80, "us-east-1a");
  addEndpointInZone(90, "us-east-1b");
  updateEndpoints();
  passActiveHealthCheckForAllHosts();
  EXPECT_TRUE(hostSet(0).hostsPerLocality().hasLocalLocality());

  // Replace 80 in local zone us-east-1a with warming 81 and remove non-local zone us-east-1b.
  cluster_load_assignment_.clear_endpoints();
  addEndpointInZone(81, "us-east-1a");
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80", "1.2.3.4:90"));
  ASSERT_EQ(2UL, hostSet(0).hostsPerLocality().get().size());
  EXPECT_TRUE(hostSet(0).hostsPerLocality().hasLocalLocality());

  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  passActiveHealthCheck(findHost("1.2.3.4:81"));
  timer->invokeCallback();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:81"));
  ASSERT_EQ(1UL, hostSet(0).hostsPerLocality().get().size());
  EXPECT_EQ("us-east-1a", hostSet(0).hostsPerLocality().get()[0][0]->locality().zone());
  EXPECT_TRUE(hostSet(0).hostsPerLocality().hasLocalLocality());
}

// Validate that pruning an empty local locality bucket (bucket 0) when removing retained hosts
// clears hasLocalLocality() to false when only a non-local locality bucket remains.
TEST_F(EdsGracefulHostRemovalTest, LocalLocalityClearedWhenPruningEmptyLocalLocality) {
  initializeCluster();
  addEndpointInZone(80, "us-east-1a");
  updateEndpoints();
  passActiveHealthCheckForAllHosts();
  EXPECT_TRUE(hostSet(0).hostsPerLocality().hasLocalLocality());

  // Replace 80 in local zone us-east-1a with warming 91 in non-local zone us-east-1b.
  cluster_load_assignment_.clear_endpoints();
  addEndpointInZone(91, "us-east-1b");
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));
  ASSERT_EQ(2UL, hostSet(0).hostsPerLocality().get().size());
  EXPECT_TRUE(hostSet(0).hostsPerLocality().hasLocalLocality());

  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  passActiveHealthCheck(findHost("1.2.3.4:91"));
  timer->invokeCallback();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:91"));
  ASSERT_EQ(1UL, hostSet(0).hostsPerLocality().get().size());
  EXPECT_EQ("us-east-1b", hostSet(0).hostsPerLocality().get()[0][0]->locality().zone());
  EXPECT_FALSE(hostSet(0).hostsPerLocality().hasLocalLocality());
}

// Validate that an empty assignment (e.g. when the assignment times out) removes the retained hosts
// right away, since no host is warming up anymore.
TEST_F(EdsGracefulHostRemovalTest, EmptyAssignmentRemovesRetainedHosts) {
  initializeCluster();
  addEndpoint(80);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  cluster_load_assignment_.clear_endpoints();
  addEndpoint(81);
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));

  cluster_load_assignment_.clear_endpoints();
  updateEndpoints();
  EXPECT_TRUE(hostSet(0).hosts().empty());
}

// Validate that a new priority without hosts (e.g. between priorities with hosts) is created with
// the updated configuration, and notified to the callbacks of the priority set and therefore to the
// load balancers.
TEST_F(EdsGracefulHostRemovalTest, NewPriorityWithoutHostsCreatedWithConfiguration) {
  resetRingHashCluster();
  ThreadAwareLoadBalancerPtr thread_aware_lb = createThreadAwareLoadBalancer();
  initializeCluster();
  addEndpoint(80, 0);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();
  const HostSharedPtr host_80 = findHost("1.2.3.4:80");

  std::vector<uint32_t> updated_priorities;
  auto priority_update_cb = cluster_->prioritySet().addPriorityUpdateCb(
      [&](uint32_t priority, const HostVector&, const HostVector&) {
        updated_priorities.push_back(priority);
        return absl::OkStatus();
      });
  // Enable weighted priority health, and add 82 to priority 2, leaving priority 1 without hosts.
  cluster_load_assignment_.mutable_policy()->set_weighted_priority_health(true);
  addEndpoint(82, 2);
  updateEndpoints();
  ASSERT_EQ(3UL, cluster_->prioritySet().hostSetsPerPriority().size());
  EXPECT_THAT(updated_priorities, testing::ElementsAre(0, 1, 2));
  for (uint32_t priority = 0; priority < 3; ++priority) {
    EXPECT_TRUE(hostSet(priority).weightedPriorityHealth());
    EXPECT_EQ(140U, hostSet(priority).overprovisioningFactor());
  }
  EXPECT_TRUE(hostSet(1).hosts().empty());
  LoadBalancerPtr lb = thread_aware_lb->factory()->create({cluster_->prioritySet(), nullptr});
  EXPECT_EQ(host_80, lb->chooseHost(nullptr).host);
}

// Validate that a health change only reloads the healthy hosts of the priority of the host.
TEST_F(EdsGracefulHostRemovalTest, HealthChangeReloadsOnlyPriorityOfHost) {
  initializeCluster();
  addEndpoint(80, 0);
  addEndpoint(90, 1);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  std::vector<uint32_t> updated_priorities;
  auto priority_update_cb = cluster_->prioritySet().addPriorityUpdateCb(
      [&](uint32_t priority, const HostVector&, const HostVector&) {
        updated_priorities.push_back(priority);
        return absl::OkStatus();
      });
  const HostSharedPtr host_90 = findHost("1.2.3.4:90");
  host_90->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker_->runCallbacks(host_90, HealthTransition::Changed, HealthState::Unhealthy);
  EXPECT_THAT(updated_priorities, testing::ElementsAre(1));
  EXPECT_EQ(1UL, hostSet(0).healthyHosts().size());
  EXPECT_TRUE(hostSet(1).healthyHosts().empty());
}

// Validate that when the retained hosts that were scheduled to be removed are not to be removed
// anymore when the removal runs (e.g. since an update added them back in the meantime), the removal
// does not update the cluster.
TEST_F(EdsGracefulHostRemovalTest, ObsoleteRetainedHostRemovalDoesNotUpdateCluster) {
  initializeCluster();
  addEndpoint(80);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  cluster_load_assignment_.clear_endpoints();
  addEndpoint(81);
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));

  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  passActiveHealthCheck(findHost("1.2.3.4:81"));
  EXPECT_TRUE(timer->enabled());

  // 80 is added back before its removal runs.
  addEndpoint(80);
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0), testing::IsEmpty());

  uint32_t priority_update_calls = 0;
  auto priority_update_cb = cluster_->prioritySet().addPriorityUpdateCb(
      [&](uint32_t, const HostVector&, const HostVector&) {
        ++priority_update_calls;
        return absl::OkStatus();
      });
  timer->invokeCallback();
  EXPECT_EQ(0U, priority_update_calls);
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:81", "1.2.3.4:80"));
}

// Validate that the maximum host weight accounts for the retained hosts of all the priorities.
TEST_F(EdsGracefulHostRemovalTest, MaxHostWeightIncludesRetainedHosts) {
  initializeCluster();
  addEndpoint(80, 0);
  setLastEndpointWeight(100);
  addEndpoint(90, 1);
  setLastEndpointWeight(1);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();
  EXPECT_EQ(100UL, cluster_->info()->endpointStats().max_host_weight_.value());

  // Replace 80 with 81 of weight 1: 80 is retained.
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(81, 0);
  setLastEndpointWeight(1);
  addEndpoint(90, 1);
  setLastEndpointWeight(1);
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));
  EXPECT_EQ(100UL, cluster_->info()->endpointStats().max_host_weight_.value());

  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  passActiveHealthCheck(findHost("1.2.3.4:81"));
  timer->invokeCallback();
  EXPECT_THAT(hostAddresses(0), testing::ElementsAre("1.2.3.4:81"));
  EXPECT_EQ(1UL, cluster_->info()->endpointStats().max_host_weight_.value());
}

// Validate that configured common_lb_config.healthy_panic_threshold values control when removed
// hosts are retained on EDS updates and when retained hosts are dropped.
TEST_F(EdsGracefulHostRemovalTest, CustomHealthyPanicThresholdControlsRetentionAndDrop) {
  resetClusterWithHealthyPanicThreshold(75.0);
  initializeCluster();
  addEndpoint(80);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  cluster_load_assignment_.clear_endpoints();
  for (uint32_t port = 81; port <= 84; ++port) {
    addEndpoint(port);
  }
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));

  passActiveHealthCheck(findHost("1.2.3.4:81"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));
  passActiveHealthCheck(findHost("1.2.3.4:82"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));

  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  passActiveHealthCheck(findHost("1.2.3.4:83"));
  timer->invokeCallback();
  EXPECT_THAT(retainedHostAddresses(0), testing::IsEmpty());
  EXPECT_EQ(3UL, hostSet(0).healthyHosts().size());
  EXPECT_EQ(1UL, hostSet(0).excludedHosts().size());

  // Partial replacement that already meets the 75% threshold on update (3 healthy + 1 warming =
  // 75%) does not retain removed hosts at all.
  passActiveHealthCheck(findHost("1.2.3.4:84"));
  cluster_load_assignment_.clear_endpoints();
  addEndpoint(81);
  addEndpoint(82);
  addEndpoint(83);
  addEndpoint(85);
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0), testing::IsEmpty());
  EXPECT_THAT(hostAddresses(0),
              testing::ElementsAre("1.2.3.4:81", "1.2.3.4:82", "1.2.3.4:83", "1.2.3.4:85"));
}

// Validate that when healthy_panic_threshold is configured to 0% (panic mode disabled), removed
// hosts are still retained when 0 endpoints can serve traffic, and dropped as soon as 1 endpoint
// becomes usable.
TEST_F(EdsGracefulHostRemovalTest, ZeroHealthyPanicThresholdRetainsUntilFirstUsableHost) {
  resetClusterWithHealthyPanicThreshold(0.0);
  initializeCluster();
  addEndpoint(80);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  cluster_load_assignment_.clear_endpoints();
  addEndpoint(81);
  addEndpoint(82);
  addEndpoint(83);
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));

  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  passActiveHealthCheck(findHost("1.2.3.4:81"));
  timer->invokeCallback();
  EXPECT_THAT(retainedHostAddresses(0), testing::IsEmpty());
  EXPECT_EQ(1UL, hostSet(0).healthyHosts().size());
  EXPECT_EQ(2UL, hostSet(0).excludedHosts().size());
}

// Validate that runtime key upstream.healthy_panic_threshold overrides the configured threshold.
TEST_F(EdsGracefulHostRemovalTest, RuntimeHealthyPanicThresholdOverride) {
  EXPECT_CALL(server_context_.runtime_loader_.snapshot_,
              getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(80));
  initializeCluster();
  addEndpoint(80);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  cluster_load_assignment_.clear_endpoints();
  for (uint32_t port = 81; port <= 85; ++port) {
    addEndpoint(port);
  }
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));

  // At 3/5 = 60% (< 80% runtime override, though >= 50% default), 80 stays retained.
  passActiveHealthCheck(findHost("1.2.3.4:81"));
  passActiveHealthCheck(findHost("1.2.3.4:82"));
  passActiveHealthCheck(findHost("1.2.3.4:83"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));

  // At 4/5 = 80%, 80 is dropped while 85 is still warming up.
  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  passActiveHealthCheck(findHost("1.2.3.4:84"));
  timer->invokeCallback();
  EXPECT_THAT(retainedHostAddresses(0), testing::IsEmpty());
  EXPECT_EQ(4UL, hostSet(0).healthyHosts().size());
  EXPECT_EQ(1UL, hostSet(0).excludedHosts().size());
}

// Validate that when all warming hosts complete their initial active health check while usable
// capacity remains below the healthy panic threshold, retained hosts are still removed so
// unreachable replacement endpoints never retain removed hosts indefinitely.
TEST_F(EdsGracefulHostRemovalTest, RetainedHostsRemovedWhenWarmupEndsBelowPanicThreshold) {
  initializeCluster();
  addEndpoint(80);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  cluster_load_assignment_.clear_endpoints();
  for (uint32_t port = 81; port <= 84; ++port) {
    addEndpoint(port);
  }
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));

  // 81 passes (1/4 = 25% < 50%), and 82 and 83 fail their initial active health check while 84 is
  // still warming: 80 stays retained.
  passActiveHealthCheck(findHost("1.2.3.4:81"));
  failInitialActiveHealthCheck(findHost("1.2.3.4:82"));
  failInitialActiveHealthCheck(findHost("1.2.3.4:83"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));

  // Once 84 also fails its initial active health check, no host is warming up anymore, so 80 is
  // removed even though only 1/4 (25% < 50%) is healthy.
  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  failInitialActiveHealthCheck(findHost("1.2.3.4:84"));
  timer->invokeCallback();
  EXPECT_THAT(retainedHostAddresses(0), testing::IsEmpty());
  EXPECT_THAT(hostAddresses(0),
              testing::ElementsAre("1.2.3.4:81", "1.2.3.4:82", "1.2.3.4:83", "1.2.3.4:84"));
  EXPECT_EQ(1UL, hostSet(0).healthyHosts().size());
}

// Validate that cross-priority retention for a priority without warming hosts of its own keeps
// removed hosts until another priority reaches the healthy panic threshold.
TEST_F(EdsGracefulHostRemovalTest, CrossPriorityRetentionUsesHealthyPanicThreshold) {
  initializeCluster();
  addEndpoint(80, 0);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();

  // Empty priority 0 and add 4 warming endpoints in priority 1.
  cluster_load_assignment_.clear_endpoints();
  for (uint32_t port = 91; port <= 94; ++port) {
    addEndpoint(port, 1);
  }
  updateEndpoints();
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));

  // 91 passes HC in priority 1 (1/4 = 25% < 50%): 80 stays retained in priority 0.
  passActiveHealthCheck(findHost("1.2.3.4:91"));
  EXPECT_THAT(retainedHostAddresses(0), testing::ElementsAre("1.2.3.4:80"));

  // 92 passes HC in priority 1 (2/4 = 50% >= 50%): 80 is removed while 93 and 94 continue warming
  // up.
  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  passActiveHealthCheck(findHost("1.2.3.4:92"));
  timer->invokeCallback();
  EXPECT_TRUE(hostSet(0).hosts().empty());
  EXPECT_EQ(2UL, hostSet(1).healthyHosts().size());
}

// Validate that if a reload was deferred during an active batch update, a subsequent single-host
// health transition reloads all priorities rather than taking the single-priority fast path.
TEST_F(EdsGracefulHostRemovalTest,
       DeferredReloadDuringBatchUpdateReloadsAllPrioritiesOnSubsequentHealthChange) {
  initializeCluster();
  addEndpoint(80, 0);
  addEndpoint(90, 1);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();
  const HostSharedPtr host_80 = findHost("1.2.3.4:80");
  const HostSharedPtr host_90 = findHost("1.2.3.4:90");

  Event::MockTimer* timer = expectGracefulHostRemovalTimer();
  FunctionBatchUpdate batch_update([&]() {
    host_80->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
    health_checker_->runCallbacks(host_80, HealthTransition::Changed, HealthState::Unhealthy);
  });
  cluster_->prioritySet().batchHostUpdate(batch_update);
  EXPECT_TRUE(timer->enabled());
  EXPECT_EQ(1UL, hostSet(0).healthyHosts().size());

  // Before the deferred timer fires, host_90 in priority 1 fails active health checking: both
  // priority 0 and priority 1 must be reloaded.
  host_90->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker_->runCallbacks(host_90, HealthTransition::Changed, HealthState::Unhealthy);
  EXPECT_TRUE(hostSet(0).healthyHosts().empty());
  EXPECT_TRUE(hostSet(1).healthyHosts().empty());
}

// Validate that max_host_weight defaults to 1 (matching BaseDynamicClusterImpl) when all hosts are
// removed.
TEST_F(EdsGracefulHostRemovalTest, MaxHostWeightDefaultsToOneWhenAllHostsRemoved) {
  initializeCluster();
  addEndpoint(80);
  setLastEndpointWeight(50);
  updateEndpoints();
  passActiveHealthCheckForAllHosts();
  EXPECT_EQ(50UL, cluster_->info()->endpointStats().max_host_weight_.value());

  cluster_load_assignment_.clear_endpoints();
  updateEndpoints();
  EXPECT_EQ(1UL, cluster_->info()->endpointStats().max_host_weight_.value());
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
        return absl::OkStatus();
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
        return absl::OkStatus();
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
        return absl::OkStatus();
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

// Verifies that if a endpoint is moved to a new priority multiple times, the health
// check value is preserved.
TEST_F(EdsTest, EndpointMovedToNewPriorityRepeated) {
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

  std::vector<uint32_t> priority_levels = {1, 2, 3, 2, 1};
  for (uint32_t priority : priority_levels) {
    cluster_load_assignment.clear_endpoints();
    add_endpoint(80, priority);
    add_endpoint(81, priority);

    doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

    {
      for (uint32_t i = 0; i < cluster_->prioritySet().hostSetsPerPriority().size(); i++) {
        auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[i]->hosts();
        if (i == priority) {
          // Priorities equal to this one should have the endpoints with port 80 and 81
          EXPECT_EQ(hosts.size(), 2);
        } else {
          // Priorities not equal to this one should now be empty.
          EXPECT_EQ(hosts.size(), 0);
        }
      }
    }

    {
      auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[priority]->hosts();

      // The endpoints were healthy, so moving them around should preserve that.
      EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
      EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
      EXPECT_FALSE(hosts[1]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
      EXPECT_FALSE(hosts[1]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
    }
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
        return absl::OkStatus();
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
        return absl::OkStatus();
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
        return absl::OkStatus();
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
        return absl::OkStatus();
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
      EXPECT_OK(eds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "")), EnvoyException,
      "Unexpected non-zero priority for local cluster 'name'.");

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
      EXPECT_OK(eds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "")), EnvoyException,
      "malformed IP address: foo.bar.com. Consider setting resolver_name or "
      "setting cluster type to 'STRICT_DNS' or 'LOGICAL_DNS'");
}

class EdsAssignmentTimeoutTest : public EdsTest {
public:
  EdsAssignmentTimeoutTest() {
    EXPECT_CALL(server_context_.dispatcher_, createTimer_(_))
        .WillOnce([this](Event::TimerCb cb) {
          timer_cb_ = cb;
          EXPECT_EQ(nullptr, interval_timer_);
          interval_timer_ = new Event::MockTimer();
          return interval_timer_;
        })
        .WillRepeatedly([](Event::TimerCb) { return new Event::MockTimer(); });

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

class EdsWithLedsTest : public EdsTest {
public:
  EdsWithLedsTest() {
    ON_CALL(server_context_.cluster_manager_.subscription_factory_,
            collectionSubscriptionFromUrl(_, _, _, _, _, _))
        .WillByDefault(
            [this](const xds::core::v3::ResourceLocator&,
                   const envoy::config::core::v3::ConfigSource&, absl::string_view, Stats::Scope&,
                   Envoy::Config::SubscriptionCallbacks& callbacks,
                   Envoy::Config::OpaqueResourceDecoderSharedPtr) -> Config::SubscriptionPtr {
              auto ret = std::make_unique<NiceMock<Config::MockSubscription>>();
              leds_callbacks_ = &callbacks;
              return ret;
            });
  }

  envoy::config::endpoint::v3::ClusterLoadAssignment
  makeLedsClusterLoadAssignment(const std::string& cluster_name) {
    envoy::config::endpoint::v3::ClusterLoadAssignment cla;
    cla.set_cluster_name(cluster_name);
    auto* endpoints = cla.add_endpoints();
    auto* locality = endpoints->mutable_locality();
    locality->set_region("us-east-1");
    locality->set_zone("us-east-1a");
    auto* leds_conf = endpoints->mutable_leds_cluster_locality_config();
    leds_conf->set_leds_collection_name(
        "xdstp://test/envoy.config.endpoint.v3.LbEndpoint/foo-endpoints/*");
    auto* leds_config_source = leds_conf->mutable_leds_config()->mutable_api_config_source();
    leds_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);
    leds_config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("xds_cluster");
    return cla;
  }

  Config::SubscriptionCallbacks* leds_callbacks_{};
};

// Regression test: a LEDS callback firing after a subsequent EDS update must
// not dereference the stale cluster_load_assignment_ pointer.
TEST_F(EdsWithLedsTest, LedsCallbackAfterSubsequentEdsUpdateNoCrash) {
  initialize();

  auto cla = makeLedsClusterLoadAssignment("fare");

  // First EDS update creates the LEDS subscription (not yet updated).
  doOnConfigUpdateVerifyNoThrow(cla);
  ASSERT_NE(nullptr, leds_callbacks_);

  // Second EDS update destroys the old cluster_load_assignment_ but keeps
  // the existing LEDS subscription.
  doOnConfigUpdateVerifyNoThrow(cla);

  // LEDS failure triggers the callback; would segfault on the dangling pointer
  // without the fix.
  leds_callbacks_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::FetchTimedout,
                                        nullptr);

  EXPECT_TRUE(initialized_);
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
}

class EdsCachedAssignmentTest : public testing::Test {
public:
  EdsCachedAssignmentTest() { resetCluster(); }

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
        .WillOnce([this](Event::TimerCb cb) {
          timer_cb_pre_ = cb;
          EXPECT_EQ(nullptr, interval_timer_pre_);
          interval_timer_pre_ = new Event::MockTimer();
          return interval_timer_pre_;
        })
        .WillRepeatedly([](Event::TimerCb) { return new Event::MockTimer(); });

    eds_cluster_ = parseClusterFromV3Yaml(yaml_config);
    Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context_, nullptr, nullptr,
                                                               true);
    ON_CALL(server_context_.cluster_manager_, edsResourcesCache())
        .WillByDefault(
            [this]() -> Config::EdsResourcesCacheOptRef { return eds_resources_cache_; });
    cluster_pre_ = *EdsClusterImpl::create(eds_cluster_, factory_context);
    EXPECT_EQ(initialize_phase, cluster_pre_->initializePhase());
    eds_callbacks_pre_ = server_context_.cluster_manager_.subscription_factory_.callbacks_;
  }

  void initialize() {
    EXPECT_CALL(*server_context_.cluster_manager_.subscription_factory_.subscription_, start(_));
    cluster_pre_->initialize([this] {
      initialized_ = true;
      return absl::OkStatus();
    });
  }

  void doOnConfigUpdateVerifyNoThrowPre(
      const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment) {
    const auto decoded_resources =
        TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
    EXPECT_OK(eds_callbacks_pre_->onConfigUpdate(decoded_resources.refvec_, ""));
  }

  void doOnConfigUpdateVerifyNoThrowPost(
      const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment) {
    const auto decoded_resources =
        TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
    EXPECT_OK(eds_callbacks_post_->onConfigUpdate(decoded_resources.refvec_, ""));
  }
  // Emulates a CDS update that creates a new cluster object with the same name,
  // that waits for EDS response.
  void updateCluster() {
    EXPECT_CALL(server_context_.dispatcher_, createTimer_(_))
        .WillOnce([this](Event::TimerCb cb) {
          timer_cb_post_ = cb;
          EXPECT_EQ(nullptr, interval_timer_post_);
          interval_timer_post_ = new Event::MockTimer();
          return interval_timer_post_;
        })
        .WillRepeatedly([](Event::TimerCb) { return new Event::MockTimer(); });

    Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context_, nullptr, nullptr,
                                                               true);
    cluster_post_ = *EdsClusterImpl::create(eds_cluster_, factory_context);
    // EXPECT_EQ(initialize_phase, cluster_post_->initializePhase());
    eds_callbacks_post_ = server_context_.cluster_manager_.subscription_factory_.callbacks_;

    EXPECT_CALL(*server_context_.cluster_manager_.subscription_factory_.subscription_, start(_));
    cluster_post_->initialize([this] {
      initialized_post_ = true;
      return absl::OkStatus();
    });
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

// Tests related to xDS-TP based configs EDS subscriptions.
class XdstpConfigsEdsTest : public testing::Test, public Event::TestUsingSimulatedTime {
public:
  XdstpConfigsEdsTest() {
    // Once envoy.reloadable_features.xdstp_based_config_singleton_subscriptions
    // is set to true by default, this should be removed.
    scoped_runtime_.mergeValues(
        {{"envoy.reloadable_features.xdstp_based_config_singleton_subscriptions", "true"}});
    ON_CALL(server_context_.xds_manager_,
            subscribeToSingletonResource(_, _, _, _, testing::An<Config::SubscriptionCallbacks&>(),
                                         _, _))
        .WillByDefault(
            [this](absl::string_view, OptRef<const envoy::config::core::v3::ConfigSource>,
                   absl::string_view, Stats::Scope&, Config::SubscriptionCallbacks& callbacks,
                   Config::OpaqueResourceDecoderSharedPtr,
                   const Config::SubscriptionOptions&) -> absl::StatusOr<Config::SubscriptionPtr> {
              auto ret = std::make_unique<NiceMock<Config::MockSubscription>>();
              subscription_ = ret.get();
              eds_callbacks_ = &callbacks;
              return ret;
            });
    ON_CALL(server_context_.xds_manager_, subscriptionFactory())
        .WillByDefault(ReturnRef(server_context_.cluster_manager_.subscription_factory_));
    resetCluster();
  }

  void resetCluster() {
    resetCluster(R"EOF(
      name: xdstp://test/envoy.config.endpoint.v3.ClusterLoadAssignment/foo-cluster/baz
      connect_timeout: 0.25s
      type: EDS
      lb_policy: ROUND_ROBIN
      eds_cluster_config:
        service_name: xdstp://test/envoy.config.endpoint.v3.ClusterLoadAssignment/foo-cluster/baz
    )EOF",
                 Cluster::InitializePhase::Secondary);
  }

  void resetCluster(const std::string& yaml_config, Cluster::InitializePhase initialize_phase) {
    server_context_.local_info_.node_.mutable_locality()->set_zone("us-east-1a");
    eds_cluster_ = parseClusterFromV3Yaml(yaml_config);
    Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context_, nullptr, nullptr,
                                                               false);
    cluster_ = *EdsClusterImpl::create(eds_cluster_, factory_context);
    EXPECT_EQ(initialize_phase, cluster_->initializePhase());
  }

  void initialize() {
    EXPECT_CALL(server_context_, timeSource()).WillRepeatedly(testing::ReturnRef(simTime()));
    EXPECT_CALL(*subscription_, start(_));
    cluster_->initialize([this] {
      initialized_ = true;
      return absl::OkStatus();
    });
  }

  void doOnConfigUpdateVerifyNoThrow(
      const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment) {
    const auto decoded_resources =
        TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
    EXPECT_OK(eds_callbacks_->onConfigUpdate(decoded_resources.refvec_, ""));
  }

  TestScopedRuntime scoped_runtime_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  bool initialized_{};
  Stats::TestUtil::TestStore& stats_ = server_context_.store_;
  NiceMock<Ssl::MockContextManager> ssl_context_manager_;

  envoy::config::cluster::v3::Cluster eds_cluster_;
  EdsClusterImplSharedPtr cluster_;
  Config::SubscriptionCallbacks* eds_callbacks_{};
  Config::MockSubscription* subscription_;
};

// Validate that xDS-TP based config invokes the xds-manager using SotW.
TEST_F(XdstpConfigsEdsTest, OnConfigUpdateSuccess) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name(
      "xdstp://test/envoy.config.endpoint.v3.ClusterLoadAssignment/foo-cluster/baz");
  initialize();
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(initialized_);
  EXPECT_EQ(1UL, stats_
                     .findCounterByString(
                         "cluster.xdstp_test/envoy.config.endpoint.v3.ClusterLoadAssignment/"
                         "foo-cluster/baz.update_no_rebuild")
                     .value()
                     .get()
                     .value());
}

// Validate that xDS-TP based config invokes the xds-manager using delta-xDS.
TEST_F(XdstpConfigsEdsTest, DeltaOnConfigUpdateSuccess) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name(
      "xdstp://test/envoy.config.endpoint.v3.ClusterLoadAssignment/foo-cluster/baz");
  initialize();

  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> resources;
  auto* resource = resources.Add();
  std::ignore = resource->mutable_resource()->PackFrom(cluster_load_assignment);
  resource->set_version("v1");
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::endpoint::v3::ClusterLoadAssignment>(
          resources, "cluster_name");
  EXPECT_OK(eds_callbacks_->onConfigUpdate(decoded_resources.refvec_, {}, "v1"));

  EXPECT_TRUE(initialized_);
  EXPECT_EQ(1UL, stats_
                     .findCounterByString(
                         "cluster.xdstp_test/envoy.config.endpoint.v3.ClusterLoadAssignment/"
                         "foo-cluster/baz.update_no_rebuild")
                     .value()
                     .get()
                     .value());
}
} // namespace
} // namespace Upstream
} // namespace Envoy
