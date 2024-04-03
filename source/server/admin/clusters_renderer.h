#pragma once

#include <cstdint>
#include <functional>

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

class ClustersTextRenderer : public ClustersRenderer {
public:
  ClustersTextRenderer(uint64_t chunk_limit, Http::ResponseHeaderMap& response_headers,
                       Buffer::Instance& response,
                       const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map);
  bool nextChunk() override;

private:
  void render(std::reference_wrapper<const Upstream::Cluster> cluster) override;
  const uint64_t chunk_limit_;
  Http::ResponseHeaderMap& response_headers_;
  Buffer::Instance& response_;
  const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map_;
  Upstream::ClusterManager::ClusterInfoMap::const_iterator it_;
};

class ClustersJsonRenderer : public ClustersRenderer {
public:
  ClustersJsonRenderer(uint64_t chunk_limit, Http::ResponseHeaderMap& response_headers,
                       Buffer::Instance& response,
                       const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map);
  bool nextChunk() override;

private:
  void render(std::reference_wrapper<const Upstream::Cluster> cluster) override;
  const uint64_t chunk_limit_;
  Http::ResponseHeaderMap& response_headers_;
  Buffer::Instance& response_;
  const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map_;
  Upstream::ClusterManager::ClusterInfoMap::const_iterator it_;
};

} // namespace Server
} // namespace Envoy
