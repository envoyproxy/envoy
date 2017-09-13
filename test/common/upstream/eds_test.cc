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
    local_info_.zone_name_ = "us-east-1a";
    eds_cluster_ = parseSdsClusterFromJson(json_config, eds_config);
    cluster_.reset(new EdsClusterImpl(eds_cluster_, runtime_, stats_, ssl_context_manager_,
                                      local_info_, cm_, dispatcher_, random_, false));
    EXPECT_EQ(Cluster::InitializePhase::Secondary, cluster_->initializePhase());
  }

  Stats::IsolatedStoreImpl stats_;
  Ssl::MockContextManager ssl_context_manager_;
  envoy::api::v2::Cluster eds_cluster_;
  MockClusterManager cm_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<EdsClusterImpl> cluster_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
};

// Validate that onConfigUpdate() with unexpected cluster names rejects config.
TEST_F(EdsTest, OnWrongNameConfigUpdate) {
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("wrong name");
  bool initialized = false;
  cluster_->setInitializedCb([&initialized] { initialized = true; });
  EXPECT_THROW(cluster_->onConfigUpdate(resources), EnvoyException);
  cluster_->onConfigUpdateFailed(nullptr);
  EXPECT_TRUE(initialized);
}

// Validate that onConfigUpdate() with unexpected cluster vector size rejects config.
TEST_F(EdsTest, OnWrongSizeConfigUpdate) {
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  // Too few.
  bool initialized = false;
  cluster_->setInitializedCb([&initialized] { initialized = true; });
  EXPECT_THROW(cluster_->onConfigUpdate(resources), EnvoyException);
  cluster_->onConfigUpdateFailed(nullptr);
  EXPECT_TRUE(initialized);
  // Too many.
  initialized = false;
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
TEST_F(EdsTest, OnSuccessConfigUpdate) {
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

} // namespace Upstream
} // namespace Envoy
