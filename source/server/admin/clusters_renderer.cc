#include "source/server/admin/clusters_renderer.h"

#include "envoy/buffer/buffer.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Server {

ClustersJsonRenderer::ClustersJsonRenderer(
    Buffer::Instance& response, const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map,
    uint64_t chunk_limit)
    : response_{response}, cluster_info_map_{cluster_info_map}, chunk_limit_{chunk_limit} {}

// TODO(demitriswan) implement using iterator state.
bool ClustersJsonRenderer::nextChunk() {
  UNREFERENCED_PARAMETER(response_);
  UNREFERENCED_PARAMETER(chunk_limit_);
  UNREFERENCED_PARAMETER(cluster_info_map_);
  return false;
}

// TODO(demitriswan) implement. See clusters_handler.cc.
void ClustersJsonRenderer::render(std::reference_wrapper<const Upstream::Cluster> cluster) {
  UNREFERENCED_PARAMETER(cluster);
}

ClustersTextRenderer::ClustersTextRenderer(
    Buffer::Instance& response, const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map,
    uint64_t chunk_limit)
    : response_{response}, cluster_info_map_{cluster_info_map}, chunk_limit_{chunk_limit} {}

// TODO(demitriswan) implement using iterator state.
bool ClustersTextRenderer::nextChunk() {
  UNREFERENCED_PARAMETER(response_);
  UNREFERENCED_PARAMETER(chunk_limit_);
  UNREFERENCED_PARAMETER(cluster_info_map_);
  return false;
}

// TODO(demitriswan) implement. See clusters_handler.cc.
void ClustersTextRenderer::render(std::reference_wrapper<const Upstream::Cluster> cluster) {
  UNREFERENCED_PARAMETER(cluster);
}

} // namespace Server
} // namespace Envoy
