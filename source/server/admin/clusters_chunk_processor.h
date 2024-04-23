#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "envoy/admin/v3/clusters.pb.h"
#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/outlier_detection.h"
#include "envoy/upstream/resource_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/json/json_streamer.h"

namespace Envoy {
namespace Server {

struct ClustersJsonContext {
  ClustersJsonContext(std::unique_ptr<Json::Streamer> streamer,
                      std::reference_wrapper<Buffer::Instance> buffer,
                      Json::Streamer::MapPtr root_map, Json::Streamer::ArrayPtr clusters)
      : streamer_(std::move(streamer)), buffer_(buffer.get()), root_map_(std::move(root_map)),
        clusters_(std::move(clusters)) {}
  std::unique_ptr<Json::Streamer> streamer_;
  Buffer::Instance& buffer_;
  Json::Streamer::MapPtr root_map_;
  Json::Streamer::ArrayPtr clusters_;
};

class ClustersChunkProcessor {
public:
  virtual bool nextChunk(Buffer::Instance& response) PURE;
  virtual ~ClustersChunkProcessor() = default;
};

class TextClustersChunkProcessor : public ClustersChunkProcessor {
public:
  TextClustersChunkProcessor(uint64_t chunk_limit, Http::ResponseHeaderMap& response_headers,
                             const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map);
  bool nextChunk(Buffer::Instance& response) override;

private:
  void render(std::reference_wrapper<const Upstream::Cluster> cluster, Buffer::Instance& response);

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

class JsonClustersChunkProcessor : public ClustersChunkProcessor {
public:
  JsonClustersChunkProcessor(uint64_t chunk_limit, Http::ResponseHeaderMap& response_headers,
                             const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map);
  bool nextChunk(Buffer::Instance& response) override;

private:
  void render(std::reference_wrapper<const Upstream::Cluster> cluster, Buffer::Instance& response);
  void drainBufferIntoResponse(Buffer::Instance& response);
  void finalize(Buffer::Instance& response);
  void addMapEntries(Json::Streamer::Map* raw_map_ptr, Buffer::Instance& response,
                     std::vector<const Json::Streamer::Map::NameValue>& entries);
  void addCircuitBreakerSettingsAsJson(Json::Streamer::Array* raw_map_ptr, Buffer::Instance& response,
                                       const envoy::config::core::v3::RoutingPriority& priority,
                                       Upstream::ResourceManager& resource_manager);

  const uint64_t chunk_limit_;
  Http::ResponseHeaderMap& response_headers_;
  std::vector<std::reference_wrapper<const Upstream::Cluster>> clusters_;
  Buffer::OwnedImpl buffer_;
  std::vector<std::unique_ptr<ClustersJsonContext>> json_context_holder_;
  uint64_t idx_;
};

} // namespace Server
} // namespace Envoy
