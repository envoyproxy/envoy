#pragma once

#include <cstdint>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Server {

class ClustersRenderer {
public:
  virtual bool nextChunk() PURE;
  // TODO(demitriswan) make sure this is the best way to handle destruction
  // in a virtual base class such as this.
  virtual ~ClustersRenderer() = default;

protected:
  virtual void render(std::reference_wrapper<const Upstream::Cluster> cluster) PURE;
};

class ClustersTextRenderer : ClustersRenderer {
public:
  ClustersTextRenderer(Buffer::Instance& response,
                       const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map,
                       uint64_t chunk_limit);
  bool nextChunk() override;

private:
  void render(std::reference_wrapper<const Upstream::Cluster> cluster) override;

  Buffer::Instance& response_;
  const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map_;
  const uint64_t chunk_limit_;
};

class ClustersJsonRenderer : ClustersRenderer {
public:
  ClustersJsonRenderer(Buffer::Instance& response,
                       const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map,
                       uint64_t chunk_limit);
  bool nextChunk() override;

private:
  void render(std::reference_wrapper<const Upstream::Cluster> cluster) override;

  Buffer::Instance& response_;
  const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map_;
  const uint16_t chunk_limit_;
};

} // namespace Server
} // namespace Envoy
