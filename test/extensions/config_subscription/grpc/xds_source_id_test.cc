#include "source/extensions/config_subscription/grpc/xds_source_id.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

TEST(XdsConfigSourceIdTest, ToKey) {
  auto cluster_source =
      XdsConfigSourceId("cluster_A", "envoy.extensions.transport_sockets.tls.v3.Secret");
  EXPECT_EQ(cluster_source.toKey(), "cluster_A+envoy.extensions.transport_sockets.tls.v3.Secret");

  auto host_source =
      XdsConfigSourceId("trafficdirector.googleapis.com", "envoy.service.runtime.v3.Runtime");
  EXPECT_EQ(host_source.toKey(), "trafficdirector.googleapis.com+envoy.service.runtime.v3.Runtime");

  auto empty_authority = XdsConfigSourceId("", "envoy.service.runtime.v3.Runtime");
  EXPECT_EQ(empty_authority.toKey(), "+envoy.service.runtime.v3.Runtime");

  auto empty_type_url = XdsConfigSourceId("cluster_A", "");
  EXPECT_EQ(empty_type_url.toKey(), "cluster_A+");
}

} // namespace
} // namespace Config
} // namespace Envoy
