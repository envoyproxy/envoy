#pragma once

#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Http {

/**
 * General utilities for HTTP filters.
 */
class FilterUtility {
public:
  /**
   * Resolve the cluster info.
   * @param decoder_callbacks supplies the decoder callback of filter.
   * @param cm supplies the cluster manager of the filter.
   */
  static Upstream::ClusterInfoConstSharedPtr
  resolveClusterInfo(Http::StreamDecoderFilterCallbacks* decoder_callbacks,
                     Upstream::ClusterManager& cm);
};

} // namespace Http
} // namespace Envoy
