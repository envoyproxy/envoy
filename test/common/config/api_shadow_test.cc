#include "envoy/api/v3alpha/cds.pb.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

// Validate that deprecated fields are accessible via the shadow protos.
TEST(ApiShadowTest, All) {
  envoy::api::v3alpha::Cluster cluster;

  cluster.mutable_hidden_envoy_deprecated_tls_context();
  cluster.set_lb_policy(envoy::api::v3alpha::Cluster::hidden_envoy_deprecated_ORIGINAL_DST_LB);
}

} // namespace
} // namespace Config
} // namespace Envoy
