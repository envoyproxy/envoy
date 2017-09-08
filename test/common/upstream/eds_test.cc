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

// Validate that onConfigUpdate() parses the endpoint metadata.
TEST_F(EdsTest, ParsesEndpointMetadata) {
  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");
  auto* endpoints = cluster_load_assignment->add_endpoints();

  auto string_value = ProtobufWkt::Value();
  string_value.set_string_value("string_value");

  auto bool_value = ProtobufWkt::Value();
  bool_value.set_bool_value(true);

  auto not_canary_value = ProtobufWkt::Value();
  not_canary_value.set_bool_value(false);

  auto ignored_value = ProtobufWkt::Value();
  ignored_value.set_number_value(1.1);

  auto metadata_struct = ProtobufWkt::Struct();
  (*metadata_struct.mutable_fields())["string_key"] = string_value;
  (*metadata_struct.mutable_fields())["bool_key"] = bool_value;
  (*metadata_struct.mutable_fields())[Config::MetadataEnvoyLbKeys::get().CANARY] = not_canary_value;
  (*metadata_struct.mutable_fields())["ignored_key"] = ignored_value;

  auto* endpoint = endpoints->add_lb_endpoints();
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("1.2.3.4");
  auto* endpoint_metadata = endpoint->mutable_metadata();
  (*endpoint_metadata->mutable_filter_metadata())[Config::MetadataFilters::get().ENVOY_LB] =
      metadata_struct;

  auto canary_value = ProtobufWkt::Value();
  canary_value.set_bool_value(true);

  auto canary_metadata_struct = ProtobufWkt::Struct();
  (*canary_metadata_struct.mutable_fields())[Config::MetadataEnvoyLbKeys::get().CANARY] =
      canary_value;

  auto* canary = endpoints->add_lb_endpoints();
  canary->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("2.3.4.5");
  auto* canary_metadata = canary->mutable_metadata();
  (*canary_metadata->mutable_filter_metadata())[Config::MetadataFilters::get().ENVOY_LB] =
      canary_metadata_struct;

  auto* other = endpoints->add_lb_endpoints();
  other->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("3.4.5.6");
  auto* other_metadata = other->mutable_metadata();
  (*other_metadata->mutable_filter_metadata())["unknown"] = ProtobufWkt::Struct();

  bool initialized = false;
  cluster_->setInitializedCb([&initialized] { initialized = true; });
  EXPECT_NO_THROW(cluster_->onConfigUpdate(resources));
  EXPECT_TRUE(initialized);

  auto& hosts = cluster_->hosts();
  EXPECT_EQ(hosts.size(), 3);
  EXPECT_EQ(hosts[0]->metadata().at("string_key"), std::string("string_value"));
  EXPECT_EQ(hosts[0]->metadata().at("bool_key"), std::string("true"));
  EXPECT_TRUE(hosts[0]->metadata().find("canary") == hosts[0]->metadata().end());
  EXPECT_TRUE(hosts[0]->metadata().find("ignored_key") == hosts[0]->metadata().end());
  EXPECT_FALSE(hosts[0]->canary());

  EXPECT_EQ(hosts[1]->metadata().at("canary"), std::string("true"));
  EXPECT_TRUE(hosts[1]->canary());

  EXPECT_TRUE(hosts[2]->metadata().empty());
  EXPECT_FALSE(hosts[2]->canary());
}

} // namespace Upstream
} // namespace Envoy
