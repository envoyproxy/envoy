#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/outlier_detection.h"
#include "envoy/upstream/resource_manager.h"

namespace Envoy {
namespace Server {

class ClustersRenderer {
public:
  virtual bool nextChunk(Buffer::Instance& response) PURE;
  // TODO(demitriswan) make sure this is the best way to handle destruction
  // in a virtual base class such as this.
  virtual ~ClustersRenderer() = default;

protected:
  // render is protected to suggest to implementations that this method should be
  // implemented to properly implement nextChunk.
  virtual void render(std::reference_wrapper<const Upstream::Cluster> cluster,
                      Buffer::Instance& response) PURE;
};

class ClustersTextRenderer : public ClustersRenderer {
public:
  ClustersTextRenderer(uint64_t chunk_limit, Http::ResponseHeaderMap& response_headers,
                       const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map);
  bool nextChunk(Buffer::Instance& response) override;

private:
  void render(std::reference_wrapper<const Upstream::Cluster> cluster,
              Buffer::Instance& response) override;
  static void addOutlierInfo(const std::string& cluster_name,
                             const Upstream::Outlier::Detector* outlier_detector,
                             Buffer::Instance& response);
  static void addCircuitBreakerSettings(const std::string& cluster_name,
                                        const std::string& priority_str,
                                        Upstream::ResourceManager& resource_manager,
                                        Buffer::Instance& response);
  const uint64_t chunk_limit_;
  Http::ResponseHeaderMap& response_headers_;
  std::vector<std::reference_wrapper<const Upstream::Cluster>> clusters_;
  uint64_t idx_;
};

class ClustersJsonRenderer : public ClustersRenderer {
public:
  ClustersJsonRenderer(uint64_t chunk_limit, Http::ResponseHeaderMap& response_headers,
                       const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map);
  bool nextChunk(Buffer::Instance& response) override;

private:
  void render(std::reference_wrapper<const Upstream::Cluster> cluster,
              Buffer::Instance& response) override;
  const uint64_t chunk_limit_;
  Http::ResponseHeaderMap& response_headers_;
  const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map_;
  Upstream::ClusterManager::ClusterInfoMap::const_iterator it_;
};

} // namespace Server
} // namespace Envoy
