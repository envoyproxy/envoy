#include "common/upstream/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace Utility {
namespace {

TEST(Utility, LbTypeToString) {
  EXPECT_EQ("ROUND_ROBIN", Utility::lbPolicyToString(envoy::api::v2::Cluster::ROUND_ROBIN));
  EXPECT_EQ("LEAST_REQUEST", Utility::lbPolicyToString(envoy::api::v2::Cluster::LEAST_REQUEST));
  EXPECT_EQ("RANDOM", Utility::lbPolicyToString(envoy::api::v2::Cluster::RANDOM));
  EXPECT_EQ("RING_HASH", Utility::lbPolicyToString(envoy::api::v2::Cluster::RING_HASH));
  EXPECT_EQ("MAGLEV", Utility::lbPolicyToString(envoy::api::v2::Cluster::MAGLEV));
  EXPECT_EQ("CLUSTER_PROVIDED",
            Utility::lbPolicyToString(envoy::api::v2::Cluster::CLUSTER_PROVIDED));
}

TEST(Utility, DiscoveryTypeToString) {
  EXPECT_EQ("STATIC", Utility::discoveryTypeToString(envoy::api::v2::Cluster::STATIC));
  EXPECT_EQ("STRICT_DNS", Utility::discoveryTypeToString(envoy::api::v2::Cluster::STRICT_DNS));
  EXPECT_EQ("LOGICAL_DNS", Utility::discoveryTypeToString(envoy::api::v2::Cluster::LOGICAL_DNS));
  EXPECT_EQ("EDS", Utility::discoveryTypeToString(envoy::api::v2::Cluster::EDS));
  EXPECT_EQ("ORIGINAL_DST", Utility::discoveryTypeToString(envoy::api::v2::Cluster::ORIGINAL_DST));
}

} // namespace
} // namespace Utility
} // namespace Upstream
} // namespace Envoy
