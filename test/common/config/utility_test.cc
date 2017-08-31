#include "common/config/utility.h"
#include "common/protobuf/protobuf.h"

#include "test/mocks/local_info/mocks.h"

#include "api/eds.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AtLeast;
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
      Protobuf::util::TimeUtil::MillisecondsToDuration(1234));
  EXPECT_EQ(1234, Utility::apiConfigSourceRefreshDelay(api_config_source).count());
}

} // namespace Config
} // namespace Envoy
