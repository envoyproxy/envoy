#pragma once

#include <cstdint>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/json/json_streamer.h"
#include "source/server/admin/clusters_params.h"

namespace Envoy {
namespace Server {

/**
 * ClustersRequest captures context for a streaming /clusters request.
 */
class ClustersRequest : public Admin::Request {
public:
  static constexpr uint64_t DefaultChunkLimit = 2 << 20; // 2 MB

  ClustersRequest(uint64_t chunk_limit, Instance& server, const ClustersParams& params);

  Http::Code start(Http::ResponseHeaderMap& response_headers) override;
  bool nextChunk(Buffer::Instance& response) override;

protected:
  uint64_t chunk_limit_{DefaultChunkLimit};
  Server::Instance& server_;
  const ClustersParams& params_;
  std::vector<std::reference_wrapper<const Upstream::Cluster>> clusters_;
};

/**
 * TextClustersRequest streams the response using a newline delimited text format.
 */
class TextClustersRequest : public ClustersRequest {
public:
  TextClustersRequest(uint64_t chunk_limit, Instance& server, const ClustersParams& params);
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
  uint64_t idx_;
};

/**
 * ClustersJsonContext holds an Envoy::Json::Streamer and the top-level JSON objects throughout the
 * duration of a request. When it is destructed, the buffer will terminate the array and object
 * herein. See the Envoy::Json::Streamer implementation for details.
 */
struct ClustersJsonContext {
  ClustersJsonContext(std::unique_ptr<Json::Streamer> streamer, Buffer::Instance& buffer,
                      Json::Streamer::MapPtr root_map, Json::Streamer::ArrayPtr clusters)
      : streamer_(std::move(streamer)), buffer_(buffer), root_map_(std::move(root_map)),
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
class JsonClustersRequest : public ClustersRequest {
public:
  JsonClustersRequest(uint64_t chunk_limit, Instance& server, const ClustersParams& params);
  Http::Code start(Http::ResponseHeaderMap& response_headers) override;
  bool nextChunk(Buffer::Instance& response) override;

private:
  void render(std::reference_wrapper<const Upstream::Cluster> cluster, Buffer::Instance& response);
  void drainBufferIntoResponse(Buffer::Instance& response);
  void finalize(Buffer::Instance& response);
  void addAddress(Json::Streamer::Map& map_json, const Upstream::HostSharedPtr& host,
                  Buffer::Instance& response);
  void addMapEntries(Json::Streamer::Map& map_json, Buffer::Instance& response,
                     std::vector<Json::Streamer::Map::NameValue>& entries);
  void addCircuitBreakers(Json::Streamer::Map& map_json,
                          Upstream::ClusterInfoConstSharedPtr cluster_info,
                          Buffer::Instance& response);
  void addCircuitBreakerForPriority(const envoy::config::core::v3::RoutingPriority& priority,
                                    Json::Streamer::Array& array_json, Buffer::Instance& response,
                                    Upstream::ResourceManager& resource_manager);
  void addEjectionThresholds(Json::Streamer::Map& map_json,
                             const Upstream::Cluster& unwrapped_cluster,
                             Buffer::Instance& response);
  void addHostStatuses(Json::Streamer::Map& map_json, const Upstream::Cluster& unwrapped_cluster,
                       Buffer::Instance& response);
  void processHostSet(Json::Streamer::Array& array_json, const Upstream::HostSetPtr& host_set,
                      Buffer::Instance& response);
  void processHost(Json::Streamer::Array& map_json, const Upstream::HostSharedPtr& host,
                   Buffer::Instance& response);
  void buildHostStats(Json::Streamer::Map& map_json, const Upstream::HostSharedPtr& host,
                      Buffer::Instance& response);
  void setHealthFlags(Json::Streamer::Map& map_json, const Upstream::HostSharedPtr& host,
                      Buffer::Instance& response);
  void setLocality(Json::Streamer::Map& map_json, const Upstream::HostSharedPtr& host,
                   Buffer::Instance& response);
  void setSuccessRate(Json::Streamer::Map& map_json, const Upstream::HostSharedPtr& host,
                      Buffer::Instance& response);
  void setHostname(const Upstream::HostSharedPtr& host,
                   std::vector<Json::Streamer::Map::NameValue>& top_level_entries);
  void loadHealthFlagMap(
      absl::btree_map<absl::string_view, absl::variant<bool, absl::string_view>>& flag_map,
      Upstream::Host::HealthFlag flag, const Upstream::HostSharedPtr& host);

  Buffer::OwnedImpl buffer_;
  std::vector<std::unique_ptr<ClustersJsonContext>> json_context_holder_;
  uint64_t idx_;
};

} // namespace Server
} // namespace Envoy
