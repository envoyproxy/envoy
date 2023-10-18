#pragma once

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/host_description.h"

namespace Envoy {
namespace Router {

class UpstreamRequest;

struct TimeoutData {
  std::chrono::milliseconds global_timeout_{0};
  std::chrono::milliseconds per_try_timeout_{0};
  std::chrono::milliseconds per_try_idle_timeout_{0};
};

// The interface the UpstreamRequest has to interact with the router filter.
class RouterFilterInterface {
public:
  virtual ~RouterFilterInterface() = default;

  virtual void onUpstream1xxHeaders(Http::ResponseHeaderMapPtr&& headers,
                                    UpstreamRequest& upstream_request) PURE;
  virtual void onUpstreamHeaders(uint64_t response_code, Http::ResponseHeaderMapPtr&& headers,
                                 UpstreamRequest& upstream_request, bool end_stream) PURE;
  virtual void onUpstreamData(Buffer::Instance& data, UpstreamRequest& upstream_request,
                              bool end_stream) PURE;
  virtual void onUpstreamTrailers(Http::ResponseTrailerMapPtr&& trailers,
                                  UpstreamRequest& upstream_request) PURE;
  virtual void onUpstreamMetadata(Http::MetadataMapPtr&& metadata_map) PURE;
  virtual void onUpstreamReset(Http::StreamResetReason reset_reason,
                               absl::string_view transport_failure,
                               UpstreamRequest& upstream_request) PURE;
  virtual void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host,
                                      bool pool_success) PURE;
  virtual void onPerTryTimeout(UpstreamRequest& upstream_request) PURE;
  virtual void onPerTryIdleTimeout(UpstreamRequest& upstream_request) PURE;
  virtual void onStreamMaxDurationReached(UpstreamRequest& upstream_request) PURE;

  virtual Http::StreamDecoderFilterCallbacks* callbacks() PURE;
  virtual Upstream::ClusterInfoConstSharedPtr cluster() PURE;
  virtual FilterConfig& config() PURE;
  virtual TimeoutData timeout() PURE;
  virtual absl::optional<std::chrono::milliseconds> dynamicMaxStreamDuration() const PURE;
  virtual Http::RequestHeaderMap* downstreamHeaders() PURE;
  virtual Http::RequestTrailerMap* downstreamTrailers() PURE;
  virtual bool downstreamResponseStarted() const PURE;
  virtual bool downstreamEndStream() const PURE;
  virtual uint32_t attemptCount() const PURE;
};

} // namespace Router
} // namespace Envoy
