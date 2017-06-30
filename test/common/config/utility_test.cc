#include "common/config/utility.h"

#include "test/mocks/local_info/mocks.h"

#include "api/eds.pb.h"
#include "gmock/gmock.h"
#include "google/protobuf/util/time_util.h"
#include "gtest/gtest.h"

using testing::ReturnRef;

namespace Envoy {
namespace Config {

TEST(UtilityTest, GetTypedResources) {
  envoy::api::v2::DiscoveryResponse response;
  EXPECT_EQ(0, Utility::getTypedResources<envoy::api::v2::ClusterLoadAssignment>(response).size());

  envoy::api::v2::ClusterLoadAssignment load_assignment_0;
  load_assignment_0.set_cluster_name("0");
  response.add_resources()->PackFrom(load_assignment_0);
  envoy::api::v2::ClusterLoadAssignment load_assignment_1;
  load_assignment_1.set_cluster_name("1");
  response.add_resources()->PackFrom(load_assignment_1);

  auto typed_resources =
      Utility::getTypedResources<envoy::api::v2::ClusterLoadAssignment>(response);
  EXPECT_EQ(2, typed_resources.size());
  EXPECT_EQ("0", typed_resources[0].cluster_name());
  EXPECT_EQ("1", typed_resources[1].cluster_name());
}

TEST(UtilityTest, ApiConfigSourceRefreshDelay) {
  envoy::api::v2::ApiConfigSource api_config_source;
  api_config_source.mutable_refresh_delay()->CopyFrom(
      google::protobuf::util::TimeUtil::MillisecondsToDuration(1234));
  EXPECT_EQ(1234, Utility::apiConfigSourceRefreshDelay(api_config_source).count());
}

TEST(UtilityTest, LocalInfoToNode) {
  LocalInfo::MockLocalInfo local_info;
  std::string foo_id("foo_id");
  EXPECT_CALL(local_info, nodeName()).WillOnce(ReturnRef(foo_id));
  std::string foo_zone("foo_zone");
  EXPECT_CALL(local_info, zoneName()).WillOnce(ReturnRef(foo_zone));
  envoy::api::v2::Node node;
  Utility::localInfoToNode(local_info, node);
  EXPECT_EQ("foo_id", node.id());
  EXPECT_EQ("foo_zone", node.locality().zone());
}

TEST(UtilityTest, SdsConfigToEdsConfig) {
  Upstream::SdsConfig sds_config{"sds", std::chrono::milliseconds(30000)};
  envoy::api::v2::ConfigSource config;
  Utility::sdsConfigToEdsConfig(sds_config, config);
  EXPECT_TRUE(config.has_api_config_source());
  const auto& api_config_source = config.api_config_source();
  EXPECT_EQ(envoy::api::v2::ApiConfigSource::REST_LEGACY, api_config_source.api_type());
  EXPECT_EQ(1, api_config_source.cluster_name().size());
  EXPECT_EQ("sds", api_config_source.cluster_name()[0]);
  EXPECT_EQ(30000, Utility::apiConfigSourceRefreshDelay(api_config_source).count());
}

} // namespace Config
} // namespace Envoy
