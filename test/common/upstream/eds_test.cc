#include "common/config/utility.h"
#include "common/upstream/eds.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "api/eds.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

class EdsTest : public testing::Test {
protected:
  EdsTest() {
    resetCluster(R"EOF(
    {
      "name": "name",
      "connect_timeout_ms": 250,
      "type": "sds",
      "lb_type": "round_robin",
      "service_name": "fare"
    }
    )EOF");
  }

  void resetCluster(const std::string& json_config) {
    envoy::api::v2::ConfigSource eds_config;
    eds_config.mutable_api_config_source()->add_cluster_name("eds");
    eds_config.mutable_api_config_source()->mutable_refresh_delay()->set_seconds(1);
    local_info_.node_.mutable_locality()->set_zone("us-east-1a");
    eds_cluster_ = parseSdsClusterFromJson(json_config, eds_config);
    cluster_.reset(new EdsClusterImpl(eds_cluster_, runtime_, stats_, ssl_context_manager_,
                                      local_info_, cm_, dispatcher_, random_, false));
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

// Validate that onConfigUpdate() with unexpected cluster names rejects config.
TEST_F(EdsTest, OnConfigUpdateWrongName) {
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("wrong name");
  bool initialized = false;
  cluster_->setInitializedCb([&initialized] { initialized = true; });
  EXPECT_THROW(cluster_->onConfigUpdate(resources), EnvoyException);
  cluster_->onConfigUpdateFailed(nullptr);
  EXPECT_TRUE(initialized);
}

// Validate that onConfigUpdate() with empty cluster vector size ignores config.
TEST_F(EdsTest, OnConfigUpdateEmpty) {
  bool initialized = false;
  cluster_->setInitializedCb([&initialized] { initialized = true; });
  cluster_->onConfigUpdate({});
  EXPECT_EQ(1UL, stats_.counter("cluster.name.update_empty").value());
  EXPECT_TRUE(initialized);
}

// Validate that onConfigUpdate() with unexpected cluster vector size rejects config.
TEST_F(EdsTest, OnConfigUpdateWrongSize) {
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  bool initialized = false;
  cluster_->setInitializedCb([&initialized] { initialized = true; });
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");
  cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");
  EXPECT_THROW(cluster_->onConfigUpdate(resources), EnvoyException);
  cluster_->onConfigUpdateFailed(nullptr);
  EXPECT_TRUE(initialized);
}

// Validate that onConfigUpdate() with the expected cluster accepts config.
TEST_F(EdsTest, OnConfigUpdateSuccess) {
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");
  bool initialized = false;
  cluster_->setInitializedCb([&initialized] { initialized = true; });
  EXPECT_NO_THROW(cluster_->onConfigUpdate(resources));
  EXPECT_TRUE(initialized);
}

// Validate that onConfigUpdate() with no service name accepts config.
TEST_F(EdsTest, NoServiceNameOnSuccessConfigUpdate) {
  resetCluster(R"EOF(
    {
      "name": "name",
      "connect_timeout_ms": 250,
      "type": "sds",
      "lb_type": "round_robin"
    }
    )EOF");
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("name");
  bool initialized = false;
  cluster_->setInitializedCb([&initialized] { initialized = true; });
  EXPECT_NO_THROW(cluster_->onConfigUpdate(resources));
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
  Config::Metadata::mutableMetadataValue(*endpoint->mutable_metadata(),
                                         Config::MetadataFilters::get().ENVOY_LB, "string_key")
      .set_string_value("string_value");
  Config::Metadata::mutableMetadataValue(*endpoint->mutable_metadata(), "custom_namespace",
                                         "num_key")
      .set_number_value(1.1);

  auto* canary = endpoints->add_lb_endpoints();
  canary->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("2.3.4.5");
  Config::Metadata::mutableMetadataValue(*canary->mutable_metadata(),
                                         Config::MetadataFilters::get().ENVOY_LB,
                                         Config::MetadataEnvoyLbKeys::get().CANARY)
      .set_bool_value(true);

  bool initialized = false;
  cluster_->setInitializedCb([&initialized] { initialized = true; });
  EXPECT_NO_THROW(cluster_->onConfigUpdate(resources));
  EXPECT_TRUE(initialized);

  auto& hosts = cluster_->hosts();
  EXPECT_EQ(hosts.size(), 2);
  EXPECT_EQ(hosts[0]->metadata().filter_metadata_size(), 2);
  EXPECT_EQ(Config::Metadata::metadataValue(hosts[0]->metadata(),
                                            Config::MetadataFilters::get().ENVOY_LB, "string_key")
                .string_value(),
            std::string("string_value"));
  EXPECT_EQ(Config::Metadata::metadataValue(hosts[0]->metadata(), "custom_namespace", "num_key")
                .number_value(),
            1.1);
  EXPECT_FALSE(Config::Metadata::metadataValue(hosts[0]->metadata(),
                                               Config::MetadataFilters::get().ENVOY_LB,
                                               Config::MetadataEnvoyLbKeys::get().CANARY)
                   .bool_value());
  EXPECT_FALSE(hosts[0]->canary());

  EXPECT_EQ(hosts[1]->metadata().filter_metadata_size(), 1);
  EXPECT_TRUE(Config::Metadata::metadataValue(hosts[1]->metadata(),
                                              Config::MetadataFilters::get().ENVOY_LB,
                                              Config::MetadataEnvoyLbKeys::get().CANARY)
                  .bool_value());
  EXPECT_TRUE(hosts[1]->canary());
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

  endpoints->add_lb_endpoints()
      ->mutable_endpoint()
      ->mutable_address()
      ->mutable_socket_address()
      ->set_address("1.2.3.4");
  endpoints->add_lb_endpoints()
      ->mutable_endpoint()
      ->mutable_address()
      ->mutable_socket_address()
      ->set_address("2.3.4.5");

  bool initialized = false;
  cluster_->setInitializedCb([&initialized] { initialized = true; });
  EXPECT_NO_THROW(cluster_->onConfigUpdate(resources));
  EXPECT_TRUE(initialized);

  const auto& hosts = cluster_->hosts();
  EXPECT_EQ(hosts.size(), 2);
  for (int i = 0; i < 2; ++i) {
    const auto& locality = hosts[i]->locality();
    EXPECT_EQ("oceania", locality.region());
    EXPECT_EQ("hello", locality.zone());
    EXPECT_EQ("world", locality.sub_zone());
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
  cluster_->setInitializedCb([&initialized] { initialized = true; });
  EXPECT_NO_THROW(cluster_->onConfigUpdate(resources));
  EXPECT_TRUE(initialized);

  EXPECT_EQ(2, cluster_->hostsPerLocality().size());
  EXPECT_EQ(1, cluster_->hostsPerLocality()[0].size());
  EXPECT_EQ(Locality("", "us-east-1a", ""),
            Locality(cluster_->hostsPerLocality()[0][0]->locality()));
  EXPECT_EQ(2, cluster_->hostsPerLocality()[1].size());
  EXPECT_EQ(Locality("oceania", "koala", "ingsoc"),
            Locality(cluster_->hostsPerLocality()[1][0]->locality()));
  EXPECT_EQ(Locality("oceania", "koala", "ingsoc"),
            Locality(cluster_->hostsPerLocality()[1][1]->locality()));

  add_hosts_to_locality("oceania", "koala", "eucalyptus", 3);
  add_hosts_to_locality("general", "koala", "ingsoc", 5);

  EXPECT_NO_THROW(cluster_->onConfigUpdate(resources));

  EXPECT_EQ(4, cluster_->hostsPerLocality().size());
  EXPECT_EQ(1, cluster_->hostsPerLocality()[0].size());
  EXPECT_EQ(Locality("", "us-east-1a", ""),
            Locality(cluster_->hostsPerLocality()[0][0]->locality()));
  EXPECT_EQ(5, cluster_->hostsPerLocality()[1].size());
  EXPECT_EQ(Locality("general", "koala", "ingsoc"),
            Locality(cluster_->hostsPerLocality()[1][0]->locality()));
  EXPECT_EQ(3, cluster_->hostsPerLocality()[2].size());
  EXPECT_EQ(Locality("oceania", "koala", "eucalyptus"),
            Locality(cluster_->hostsPerLocality()[2][0]->locality()));
  EXPECT_EQ(2, cluster_->hostsPerLocality()[3].size());
  EXPECT_EQ(Locality("oceania", "koala", "ingsoc"),
            Locality(cluster_->hostsPerLocality()[3][0]->locality()));
}

} // namespace Upstream
} // namespace Envoy
