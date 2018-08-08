#include <memory>

#include "envoy/api/v2/eds.pb.h"
#include "envoy/stats/scope.h"

#include "common/config/utility.h"
#include "common/upstream/eds.h"

#include "server/transport_socket_config_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Upstream {

class EdsTest : public testing::Test {
protected:
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
            cluster_names:
            - eds
            refresh_delay: 1s
    )EOF");
  }

  void resetCluster(const std::string& yaml_config) {
    local_info_.node_.mutable_locality()->set_zone("us-east-1a");
    eds_cluster_ = parseClusterFromV2Yaml(yaml_config);
    Upstream::ClusterManager::ClusterInfoMap cluster_map;
    Upstream::MockCluster cluster;
    cluster_map.emplace("eds", cluster);
    EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
    EXPECT_CALL(cluster, info()).Times(2);
    EXPECT_CALL(*cluster.info_, addedViaApi());
    Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
        "cluster.{}.",
        eds_cluster_.alt_stat_name().empty() ? eds_cluster_.name() : eds_cluster_.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_);
    cluster_.reset(
        new EdsClusterImpl(eds_cluster_, runtime_, factory_context, std::move(scope), false));
    EXPECT_EQ(Cluster::InitializePhase::Secondary, cluster_->initializePhase());
  }

  Stats::IsolatedStoreImpl stats_;
  Ssl::MockContextManager ssl_context_manager_;
  envoy::api::v2::Cluster eds_cluster_;
  NiceMock<MockClusterManager> cm_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<EdsClusterImpl> cluster_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
};

// Negative test for protoc-gen-validate constraints.
TEST_F(EdsTest, ValidateFail) {
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  resources.Add();
  EXPECT_THROW(cluster_->onConfigUpdate(resources, ""), ProtoValidationException);
}

// Validate that onConfigUpdate() with unexpected cluster names rejects config.
TEST_F(EdsTest, OnConfigUpdateWrongName) {
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("wrong name");
  bool initialized = false;
  cluster_->initialize([&initialized] { initialized = true; });
  EXPECT_THROW(cluster_->onConfigUpdate(resources, ""), EnvoyException);
  cluster_->onConfigUpdateFailed(nullptr);
  EXPECT_TRUE(initialized);
}

// Validate that onConfigUpdate() with empty cluster vector size ignores config.
TEST_F(EdsTest, OnConfigUpdateEmpty) {
  bool initialized = false;
  cluster_->initialize([&initialized] { initialized = true; });
  cluster_->onConfigUpdate({}, "");
  EXPECT_EQ(1UL, stats_.counter("cluster.name.update_empty").value());
  EXPECT_TRUE(initialized);
}

// Validate that onConfigUpdate() with unexpected cluster vector size rejects config.
TEST_F(EdsTest, OnConfigUpdateWrongSize) {
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  bool initialized = false;
  cluster_->initialize([&initialized] { initialized = true; });
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");
  cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");
  EXPECT_THROW(cluster_->onConfigUpdate(resources, ""), EnvoyException);
  cluster_->onConfigUpdateFailed(nullptr);
  EXPECT_TRUE(initialized);
}

// Validate that onConfigUpdate() with the expected cluster accepts config.
TEST_F(EdsTest, OnConfigUpdateSuccess) {
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");
  bool initialized = false;
  cluster_->initialize([&initialized] { initialized = true; });
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  EXPECT_TRUE(initialized);
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
            cluster_names:
            - eds
            refresh_delay: 1s
    )EOF");
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("name");
  bool initialized = false;
  cluster_->initialize([&initialized] { initialized = true; });
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  EXPECT_TRUE(initialized);
}

// Validate that onConfigUpdate() updates the endpoint metadata.
TEST_F(EdsTest, EndpointMetadata) {
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");
  auto* endpoints = cluster_load_assignment->add_endpoints();

  auto* endpoint = endpoints->add_lb_endpoints();
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("1.2.3.4");
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);
  Config::Metadata::mutableMetadataValue(*endpoint->mutable_metadata(),
                                         Config::MetadataFilters::get().ENVOY_LB, "string_key")
      .set_string_value("string_value");
  Config::Metadata::mutableMetadataValue(*endpoint->mutable_metadata(), "custom_namespace",
                                         "num_key")
      .set_number_value(1.1);

  auto* canary = endpoints->add_lb_endpoints();
  canary->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("2.3.4.5");
  canary->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);
  Config::Metadata::mutableMetadataValue(*canary->mutable_metadata(),
                                         Config::MetadataFilters::get().ENVOY_LB,
                                         Config::MetadataEnvoyLbKeys::get().CANARY)
      .set_bool_value(true);
  Config::Metadata::mutableMetadataValue(*canary->mutable_metadata(),
                                         Config::MetadataFilters::get().ENVOY_LB, "version")
      .set_string_value("v1");

  bool initialized = false;
  cluster_->initialize([&initialized] { initialized = true; });
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  EXPECT_TRUE(initialized);
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
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  EXPECT_EQ(1UL, stats_.counter("cluster.name.update_no_rebuild").value());

  // New resources with Metadata updated.
  Config::Metadata::mutableMetadataValue(*canary->mutable_metadata(),
                                         Config::MetadataFilters::get().ENVOY_LB, "version")
      .set_string_value("v2");
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  auto& nhosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(nhosts.size(), 2);
  EXPECT_EQ(Config::Metadata::metadataValue(*nhosts[1]->metadata(),
                                            Config::MetadataFilters::get().ENVOY_LB, "version")
                .string_value(),
            "v2");
}

// Validate that onConfigUpdate() updates endpoint health status.
TEST_F(EdsTest, EndpointHealthStatus) {
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");
  auto* endpoints = cluster_load_assignment->add_endpoints();

  // First check that EDS is correctly mapping
  // envoy::api::v2::core::HealthStatus values to the expected healthy() status.
  const std::vector<std::pair<envoy::api::v2::core::HealthStatus, bool>> health_status_expected = {
      {envoy::api::v2::core::HealthStatus::UNKNOWN, true},
      {envoy::api::v2::core::HealthStatus::HEALTHY, true},
      {envoy::api::v2::core::HealthStatus::UNHEALTHY, false},
      {envoy::api::v2::core::HealthStatus::DRAINING, false},
      {envoy::api::v2::core::HealthStatus::TIMEOUT, false},
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

  bool initialized = false;
  cluster_->initialize([&initialized] { initialized = true; });
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  EXPECT_TRUE(initialized);
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), health_status_expected.size());

    for (uint32_t i = 0; i < hosts.size(); ++i) {
      EXPECT_EQ(health_status_expected[i].second, hosts[i]->healthy());
    }
  }

  // Perform an update in which we don't change the host set, but flip some host
  // to unhealthy, check we have the expected change in status.
  endpoints->mutable_lb_endpoints(0)->set_health_status(
      envoy::api::v2::core::HealthStatus::UNHEALTHY);
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), health_status_expected.size());
    EXPECT_FALSE(hosts[0]->healthy());

    for (uint32_t i = 1; i < hosts.size(); ++i) {
      EXPECT_EQ(health_status_expected[i].second, hosts[i]->healthy());
    }
  }

  // Perform an update in which we don't change the host set, but flip some host
  // to healthy, check we have the expected change in status.
  endpoints->mutable_lb_endpoints(health_status_expected.size() - 1)
      ->set_health_status(envoy::api::v2::core::HealthStatus::HEALTHY);
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), health_status_expected.size());
    EXPECT_FALSE(hosts[0]->healthy());
    EXPECT_TRUE(hosts[hosts.size() - 1]->healthy());

    for (uint32_t i = 1; i < hosts.size() - 1; ++i) {
      EXPECT_EQ(health_status_expected[i].second, hosts[i]->healthy());
    }
  }

  // Mark host 0 unhealthy from active health checking as well, we should have
  // the same situation as above.
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    hosts[0]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  }
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_FALSE(hosts[0]->healthy());
  }

  // Now mark host 0 healthy via EDS, it should still be unhealthy due to the
  // active health check failure.
  endpoints->mutable_lb_endpoints(0)->set_health_status(
      envoy::api::v2::core::HealthStatus::HEALTHY);
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_FALSE(hosts[0]->healthy());
  }

  // Finally, mark host 0 healthy again via active health check. It should be
  // immediately healthy again.
  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    hosts[0]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
    EXPECT_TRUE(hosts[0]->healthy());
  }
}

// Validate that onConfigUpdate() removes endpoints that are marked as healthy
// when configured to do so.
TEST_F(EdsTest, EndpointRemoval) {
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
            cluster_names:
            - eds
            refresh_delay: 1s
  )EOF");

  auto health_checker = std::make_shared<MockHealthChecker>();
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_)).Times(2);
  cluster_->setHealthChecker(health_checker);

  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");

  auto add_endpoint = [cluster_load_assignment](int port) {
    auto* endpoints = cluster_load_assignment->add_endpoints();

    auto* socket_address = endpoints->add_lb_endpoints()
                               ->mutable_endpoint()
                               ->mutable_address()
                               ->mutable_socket_address();
    socket_address->set_address("1.2.3.4");
    socket_address->set_port_value(port);
  };

  add_endpoint(80);
  add_endpoint(81);

  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 2);

    EXPECT_TRUE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_TRUE(hosts[1]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    // Mark the hosts as healthy
    hosts[0]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
    hosts[1]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  }

  // Remove endpoints and add back the port 80 one
  cluster_load_assignment->clear_endpoints();
  add_endpoint(80);

  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(hosts.size(), 1);
  }
}

// Validate that onConfigUpdate() updates the endpoint locality.
TEST_F(EdsTest, EndpointLocality) {
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");
  auto* endpoints = cluster_load_assignment->add_endpoints();
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

  bool initialized = false;
  cluster_->initialize([&initialized] { initialized = true; });
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  EXPECT_TRUE(initialized);

  auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(hosts.size(), 2);
  for (int i = 0; i < 2; ++i) {
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
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");

  {
    auto* endpoints = cluster_load_assignment->add_endpoints();
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

  bool initialized = false;
  cluster_->initialize([&initialized] { initialized = true; });
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  EXPECT_TRUE(initialized);

  EXPECT_EQ(nullptr, cluster_->prioritySet().hostSetsPerPriority()[0]->localityWeights());
}

// Validate that onConfigUpdate() propagates locality weights to the host set when locality
// weighted balancing is configured.
TEST_F(EdsTest, EndpointLocalityWeights) {
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
            cluster_names:
            - eds
            refresh_delay: 1s
    )EOF");
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");

  {
    auto* endpoints = cluster_load_assignment->add_endpoints();
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
    auto* endpoints = cluster_load_assignment->add_endpoints();
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
    auto* endpoints = cluster_load_assignment->add_endpoints();
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

  bool initialized = false;
  cluster_->initialize([&initialized] { initialized = true; });
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  EXPECT_TRUE(initialized);

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
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");
  uint32_t port = 1000;
  auto add_hosts_to_locality = [cluster_load_assignment,
                                &port](const std::string& region, const std::string& zone,
                                       const std::string& sub_zone, uint32_t n, uint32_t priority) {
    auto* endpoints = cluster_load_assignment->add_endpoints();
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

  bool initialized = false;
  cluster_->initialize([&initialized] { initialized = true; });
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  EXPECT_TRUE(initialized);

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

  // Reset the ClusterLoadAssingment to only contain one of the locality per priority.
  // This should leave us with only one locality.
  cluster_load_assignment->clear_endpoints();
  add_hosts_to_locality("oceania", "koala", "ingsoc", 4, 0);
  add_hosts_to_locality("oceania", "bear", "best", 2, 1);

  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));

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
  cluster_load_assignment->clear_endpoints();
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));

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
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");
  uint32_t port = 1000;
  auto add_hosts_to_locality = [cluster_load_assignment,
                                &port](const std::string& region, const std::string& zone,
                                       const std::string& sub_zone, uint32_t n) {
    auto* endpoints = cluster_load_assignment->add_endpoints();
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

  bool initialized = false;
  cluster_->initialize([&initialized] { initialized = true; });
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  EXPECT_TRUE(initialized);

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

  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));

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
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");
  uint32_t port = 1000;
  auto add_hosts_to_locality = [cluster_load_assignment,
                                &port](const std::string& region, const std::string& zone,
                                       const std::string& sub_zone, uint32_t n, uint32_t priority) {
    auto* endpoints = cluster_load_assignment->add_endpoints();
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

  bool initialized = false;
  cluster_->initialize([&initialized] { initialized = true; });
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  EXPECT_TRUE(initialized);

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(2, hosts.size());
  }

  {
    auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[1]->hosts();
    EXPECT_EQ(1, hosts.size());
  }

  cluster_load_assignment->clear_endpoints();

  add_hosts_to_locality("oceania", "koala", "ingsoc", 4, 0);

  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));

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
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");
  uint32_t port = 1000;
  auto add_hosts_to_priority = [cluster_load_assignment, &port](uint32_t priority, uint32_t n) {
    auto* endpoints = cluster_load_assignment->add_endpoints();
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

  bool initialized = false;
  cluster_->initialize([&initialized] { initialized = true; });
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  EXPECT_TRUE(initialized);

  ASSERT_EQ(2, cluster_->prioritySet().hostSetsPerPriority().size());
  EXPECT_EQ(2, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(1, cluster_->prioritySet().hostSetsPerPriority()[1]->hosts().size());

  // Add 2 more hosts to priority 0, and add five hosts to priority 2.
  // Note the (illegal) gap (no priority 1.)  Until we have config validation,
  // make sure bad config does no harm.
  add_hosts_to_priority(0, 2);
  add_hosts_to_priority(3, 5);

  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));

  ASSERT_EQ(4, cluster_->prioritySet().hostSetsPerPriority().size());
  EXPECT_EQ(4, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(1, cluster_->prioritySet().hostSetsPerPriority()[1]->hosts().size());
  EXPECT_EQ(0, cluster_->prioritySet().hostSetsPerPriority()[2]->hosts().size());
  EXPECT_EQ(5, cluster_->prioritySet().hostSetsPerPriority()[3]->hosts().size());

  // Update the number of hosts in priority #4. Make sure no other priority
  // levels are affected.
  cluster_load_assignment->clear_endpoints();
  add_hosts_to_priority(3, 4);
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  ASSERT_EQ(4, cluster_->prioritySet().hostSetsPerPriority().size());
  EXPECT_EQ(4, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(1, cluster_->prioritySet().hostSetsPerPriority()[1]->hosts().size());
  EXPECT_EQ(0, cluster_->prioritySet().hostSetsPerPriority()[2]->hosts().size());
  EXPECT_EQ(4, cluster_->prioritySet().hostSetsPerPriority()[3]->hosts().size());
}

// Make sure config updates with P!=0 are rejected for the local cluster.
TEST_F(EdsTest, NoPriorityForLocalCluster) {
  cm_.local_cluster_name_ = "fare";
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");
  uint32_t port = 1000;
  auto add_hosts_to_priority = [cluster_load_assignment, &port](uint32_t priority, uint32_t n) {
    auto* endpoints = cluster_load_assignment->add_endpoints();
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
  bool initialized = false;
  cluster_->initialize([&initialized] { initialized = true; });
  EXPECT_THROW_WITH_MESSAGE(cluster_->onConfigUpdate(resources, ""), EnvoyException,
                            "Unexpected non-zero priority for local cluster 'fare'.");

  // Try an update which only has endpoints with P=0. This should go through.
  cluster_load_assignment->clear_endpoints();
  add_hosts_to_priority(0, 2);
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
}

// Set up an EDS config with multiple priorities and localities and make sure
// they are loaded and reloaded as expected.
TEST_F(EdsTest, PriorityAndLocality) {
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");
  uint32_t port = 1000;
  auto add_hosts_to_locality_and_priority =
      [cluster_load_assignment, &port](const std::string& region, const std::string& zone,
                                       const std::string& sub_zone, uint32_t priority, uint32_t n) {
        auto* endpoints = cluster_load_assignment->add_endpoints();
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

  bool initialized = false;
  cluster_->initialize([&initialized] { initialized = true; });
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  EXPECT_TRUE(initialized);

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

  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));

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
            cluster_names:
            - eds
            refresh_delay: 1s
    )EOF");

  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");
  uint32_t port = 1000;
  auto add_hosts_to_locality_and_priority =
      [cluster_load_assignment, &port](const std::string& region, const std::string& zone,
                                       const std::string& sub_zone, uint32_t priority, uint32_t n,
                                       uint32_t weight) {
        auto* endpoints = cluster_load_assignment->add_endpoints();
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

  bool initialized = false;
  cluster_->initialize([&initialized] { initialized = true; });
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  EXPECT_TRUE(initialized);
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
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  EXPECT_EQ(1UL, stats_.counter("cluster.name.update_no_rebuild").value());

  // Adjust locality weights, validate that we observe an update.
  cluster_load_assignment->mutable_endpoints(0)->mutable_load_balancing_weight()->set_value(60);
  cluster_load_assignment->mutable_endpoints(1)->mutable_load_balancing_weight()->set_value(40);
  VERBOSE_EXPECT_NO_THROW(cluster_->onConfigUpdate(resources, ""));
  EXPECT_EQ(1UL, stats_.counter("cluster.name.update_no_rebuild").value());
}

// Throw on adding a new resource with an invalid endpoint (since the given address is invalid).
TEST_F(EdsTest, MalformedIP) {
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");
  auto* endpoints = cluster_load_assignment->add_endpoints();

  auto* endpoint = endpoints->add_lb_endpoints();
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address(
      "foo.bar.com");
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);

  cluster_->initialize([] {});
  EXPECT_THROW_WITH_MESSAGE(cluster_->onConfigUpdate(resources, ""), EnvoyException,
                            "malformed IP address: foo.bar.com. Consider setting resolver_name or "
                            "setting cluster type to 'STRICT_DNS' or 'LOGICAL_DNS'");
}

} // namespace Upstream
} // namespace Envoy
