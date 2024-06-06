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

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

/**
 * ClustersChunkProcessor is the interface for streaming Clusters data in a response
 * for /clusters.
 */
class ClustersChunkProcessor {
public:
  virtual bool nextChunk(Buffer::Instance& response) PURE;
  virtual ~ClustersChunkProcessor() = default;
};

/**
 * TextClustersChunkProcessor streams the response using a newline delimited text format.
 */
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

/**
 * ClustersJsonContext holds an Envoy::Json::Streamer and the top-level JSON objects throughout the
 * duration of a request. When it is destructed, the buffer will terminate the array and object
 * herein. See the Envoy::Json::Streamer implementation for details.
 */
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

/**
 * JsonClustersChunkProcessor streams the response and emulates the JSON encoding behavior of
 * the gRPC clients; specifically, "zero-value" values such as false, 0, empty string, and
 * empty objects are omitted from the output.
 */
class JsonClustersChunkProcessor : public ClustersChunkProcessor {
public:
  JsonClustersChunkProcessor(uint64_t chunk_limit, Http::ResponseHeaderMap& response_headers,
                             const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map);
  bool nextChunk(Buffer::Instance& response) override;

private:
  void render(std::reference_wrapper<const Upstream::Cluster> cluster, Buffer::Instance& response);
  void drainBufferIntoResponse(Buffer::Instance& response);
  void finalize(Buffer::Instance& response);
  void addAddress(Json::Streamer::Map* raw_host_ptr, const Upstream::HostSharedPtr& host,
                  Buffer::Instance& response);
  void addMapEntries(Json::Streamer::Map* raw_map_ptr, Buffer::Instance& response,
                     std::vector<Json::Streamer::Map::NameValue>& entries);
  void addCircuitBreakers(Json::Streamer::Map* raw_clusters_map_ptr,
                          Upstream::ClusterInfoConstSharedPtr cluster_info,
                          Buffer::Instance& response);
  void addCircuitBreakerForPriority(const envoy::config::core::v3::RoutingPriority& priority,
                                    Json::Streamer::Array* raw_map_ptr, Buffer::Instance& response,
                                    Upstream::ResourceManager& resource_manager);
  void addEjectionThresholds(Json::Streamer::Map* raw_clusters_map_ptr,
                             const Upstream::Cluster& unwrapped_cluster,
                             Buffer::Instance& response);
  void addHostStatuses(Json::Streamer::Map* raw_clusters_map_ptr,
                       const Upstream::Cluster& unwrapped_cluster, Buffer::Instance& response);
  void processHostSet(Json::Streamer::Array* raw_hosts_statuses_ptr,
                      const Upstream::HostSetPtr& host_set, Buffer::Instance& response);
  void processHost(Json::Streamer::Array* raw_host_statuses_ptr,
                   const Upstream::HostSharedPtr& host, Buffer::Instance& response);
  void buildHostStats(Json::Streamer::Map* raw_host_ptr, const Upstream::HostSharedPtr& host,
                      Buffer::Instance& response);
  void setHealthFlags(Json::Streamer::Map* raw_host_ptr, const Upstream::HostSharedPtr& host,
                      Buffer::Instance& response);
  void setLocality(Json::Streamer::Map* raw_host_ptr, const Upstream::HostSharedPtr& host,
                   Buffer::Instance& response);
  void setSuccessRate(const Upstream::HostSharedPtr& host,
                      std::vector<Json::Streamer::Map::NameValue>& top_level_entries);
  void setHostname(const Upstream::HostSharedPtr& host,
                   std::vector<Json::Streamer::Map::NameValue>& top_level_entries);
  void loadHealthFlagMap(
      absl::btree_map<absl::string_view, absl::variant<bool, absl::string_view>>& flag_map,
      Upstream::Host::HealthFlag flag, const Upstream::HostSharedPtr& host);

  const uint64_t chunk_limit_;
  Http::ResponseHeaderMap& response_headers_;
  std::vector<std::reference_wrapper<const Upstream::Cluster>> clusters_;
  Buffer::OwnedImpl buffer_;
  std::vector<std::unique_ptr<ClustersJsonContext>> json_context_holder_;
  uint64_t idx_;
};

} // namespace Server
} // namespace Envoy
