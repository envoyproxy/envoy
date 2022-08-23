#include "source/common/config/xds_source_id.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

TEST(XdsConfigSourceIdTest, ToKey) {
  auto cluster_source =
      XdsConfigSourceId("cluster_A", "envoy.extensions.transport_sockets.tls.v3.Secret");
  EXPECT_EQ(cluster_source.toKey(),
            "Y2x1c3Rlcl9B+ZW52b3kuZXh0ZW5zaW9ucy50cmFuc3BvcnRfc29ja2V0cy50bHMudjMuU2VjcmV0");

  auto host_source =
      XdsConfigSourceId("trafficdirector.googleapis.com", "envoy.service.runtime.v3.Runtime");
  EXPECT_EQ(host_source.toKey(),
            "dHJhZmZpY2RpcmVjdG9yLmdvb2dsZWFwaXMuY29t+ZW52b3kuc2VydmljZS5ydW50aW1lLnYzLlJ1bnRpbWU");

  auto empty_authority = XdsConfigSourceId("", "envoy.service.runtime.v3.Runtime");
  EXPECT_EQ(empty_authority.toKey(), "+ZW52b3kuc2VydmljZS5ydW50aW1lLnYzLlJ1bnRpbWU");

  auto empty_type_url = XdsConfigSourceId("cluster_A", "");
  EXPECT_EQ(empty_type_url.toKey(), "Y2x1c3Rlcl9B+");
}

} // namespace
} // namespace Config
} // namespace Envoy
