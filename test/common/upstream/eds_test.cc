#include "common/config/utility.h"
#include "common/json/json_loader.h"
#include "common/upstream/eds.h"

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
    std::string raw_config = R"EOF(
    {
      "name": "name",
      "connect_timeout_ms": 250,
      "type": "sds",
      "lb_type": "round_robin",
      "service_name": "fare"
    }
    )EOF";

    Json::ObjectSharedPtr config = Json::Factory::loadFromString(raw_config);
    Config::Utility::sdsConfigToEdsConfig(SdsConfig{"eds", std::chrono::milliseconds(30000)},
                                          &eds_config_);
    local_info_.zone_name_ = "us-east-1a";
    cluster_.reset(new EdsClusterImpl(*config, runtime_, stats_, ssl_context_manager_, eds_config_,
                                      local_info_, cm_, dispatcher_, random_));
    EXPECT_EQ(Cluster::InitializePhase::Secondary, cluster_->initializePhase());
  }

  Stats::IsolatedStoreImpl stats_;
  Ssl::MockContextManager ssl_context_manager_;
  envoy::api::v2::ConfigSource eds_config_;
  MockClusterManager cm_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::unique_ptr<EdsClusterImpl> cluster_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
};

// Validate that onConfigUpdate() with unexpected cluster names rejects config.
TEST_F(EdsTest, OnWrongNameConfigUpdate) {
  google::protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
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
  google::protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
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

// Validate that onConfigupdate() with the expected cluster accepts config.
TEST_F(EdsTest, OnSuccessConfigUpdate) {
  google::protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name("fare");
  bool initialized = false;
  cluster_->setInitializedCb([&initialized] { initialized = true; });
  EXPECT_NO_THROW(cluster_->onConfigUpdate(resources));
  EXPECT_TRUE(initialized);
}

} // Upstream
} // Envoy
