// NOLINT(namespace-envoy)
#include <cstdlib>
#include <iostream>

#include "envoy/api/v3alpha/cds_envoy_internal.pb.h"

// Validate that deprecated fields are accessible via the shadow protos.
int main(int argc, char* argv[]) {
  envoy::api::v3alpha::Cluster cluster;

  cluster.mutable_hidden_envoy_deprecated_tls_context();
  cluster.set_lb_policy(envoy::api::v3alpha::Cluster::hidden_envoy_deprecated_ORIGINAL_DST_LB);

  exit(EXIT_SUCCESS);
}
