#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/outlier_detection.h"
#include "envoy/upstream/resource_manager.h"

#include "source/common/json/json_streamer.h"

namespace Envoy {
namespace Server {

class ClustersChunkProcessor {
public:
  virtual bool nextChunk(Buffer::Instance& response) PURE;
  virtual ~ClustersChunkProcessor() = default;
};

class ClusterRenderer {
public:
  virtual void render(std::reference_wrapper<const Upstream::Cluster> cluster,
                      Buffer::Instance& response) PURE;
  virtual ~ClusterRenderer() = default;
};

class TextClusterRenderer : public ClusterRenderer {
public:
  TextClusterRenderer() = default;
  void render(std::reference_wrapper<const Upstream::Cluster> cluster,
              Buffer::Instance& response) override;

private:
  static void addOutlierInfo(const std::string& cluster_name,
                             const Upstream::Outlier::Detector* outlier_detector,
                             Buffer::Instance& response);
  static void addCircuitBreakerSettings(const std::string& cluster_name,
                                        const std::string& priority_str,
                                        Upstream::ResourceManager& resource_manager,
                                        Buffer::Instance& response);
};

class TextClustersChunkProcessor : public ClustersChunkProcessor {
public:
  TextClustersChunkProcessor(uint64_t chunk_limit, Http::ResponseHeaderMap& response_headers,
                             const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map);

  bool nextChunk(Buffer::Instance& response) override;

private:
  const uint64_t chunk_limit_;
  Http::ResponseHeaderMap& response_headers_;
  std::vector<std::reference_wrapper<const Upstream::Cluster>> clusters_;
  std::unique_ptr<TextClusterRenderer> renderer_;
  uint64_t idx_;
};

class JsonClusterRenderer : public ClusterRenderer {
public:
  JsonClusterRenderer() = default;
  void render(std::reference_wrapper<const Upstream::Cluster> cluster,
              Buffer::Instance& response) override;
};

class JsonClustersChunkProcessor : public ClustersChunkProcessor {
public:
  JsonClustersChunkProcessor(uint64_t chunk_limit, Http::ResponseHeaderMap& response_headers,
                             const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map);

  bool nextChunk(Buffer::Instance& response) override;

private:
  const uint64_t chunk_limit_;
  Http::ResponseHeaderMap& response_headers_;
  std::vector<std::reference_wrapper<const Upstream::Cluster>> clusters_;
  std::unique_ptr<JsonClusterRenderer> renderer_;
  uint64_t idx_;
};

} // namespace Server
} // namespace Envoy
