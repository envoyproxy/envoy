#pragma once

#include "envoy/common/matchers.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Upstream {

/**
 * Collects endpoint assignments for active clusters.
 */
ProtobufTypes::MessagePtr dumpEndpointConfigs(const ClusterManager& cluster_manager,
                                              const Matchers::StringMatcher& name_matcher);

} // namespace Upstream
} // namespace Envoy
