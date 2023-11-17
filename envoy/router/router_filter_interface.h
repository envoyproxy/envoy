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

// This groups various per-stream timeouts conveniently together.
struct TimeoutData {
  std::chrono::milliseconds global_timeout_{0};
  std::chrono::milliseconds per_try_timeout_{0};
  std::chrono::milliseconds per_try_idle_timeout_{0};
};

// The interface the UpstreamRequest has to interact with the router filter.
class RouterFilterInterface {
public:
  virtual ~RouterFilterInterface() = default;

  /**
   * This will be called when upstream 1xx headers are ready to be processed by downstream code.
   * @param headers contains the 1xx headers
   * @param upstream_request inicates which UpstreamRequest the 1xx headers are from.
   *
   */
  virtual void onUpstream1xxHeaders(Http::ResponseHeaderMapPtr&& headers,
                                    UpstreamRequest& upstream_request) PURE;
  /**
   * This will be called when upstream non-1xx headers are ready to be processed by downstream code.
   * @param headers contains the headers
   * @param upstream_request inicates which UpstreamRequest the headers are from.
   * @param end_stream indicates if the response is complete.
   *
   */
  virtual void onUpstreamHeaders(uint64_t response_code, Http::ResponseHeaderMapPtr&& headers,
                                 UpstreamRequest& upstream_request, bool end_stream) PURE;
  /**
   * This will be called when upstream data is ready to be processed by downstream code.
   * @param data contains the data to process
   * @param upstream_request inicates which UpstreamRequest the data is from.
   * @param end_stream indicates if the response is complete.
   *
   */
  virtual void onUpstreamData(Buffer::Instance& data, UpstreamRequest& upstream_request,
                              bool end_stream) PURE;
  /**
   * This will be called when upstream trailers are ready to be processed by downstream code.
   * @param trailers contains the trailers to process
   * @param upstream_request inicates which UpstreamRequest the trailers are from.
   *
   */
  virtual void onUpstreamTrailers(Http::ResponseTrailerMapPtr&& trailers,
                                  UpstreamRequest& upstream_request) PURE;
  /**
   * This will be called when upstream metadata is ready to be processed by downstream code.
   * @param metadata contains the metadata to process
   * @param upstream_request inicates which UpstreamRequest the metadata is from.
   *
   */
  virtual void onUpstreamMetadata(Http::MetadataMapPtr&& metadata_map) PURE;

  /**
   * This will be called when an upstream reset is ready to be processed by downstream code.
   * @param reset_reason indicates the reason for the reset.
   * @param transport_failure optionally indicates any transport failure.
   * @param upstream_request inicates which UpstreamRequest the reset is from.
   *
   */
  virtual void onUpstreamReset(Http::StreamResetReason reset_reason,
                               absl::string_view transport_failure,
                               UpstreamRequest& upstream_request) PURE;

  /**
   * This will be called when an upstream host is selected. This is called both
   * if the host can accomodate the stream and if the host is selected but unusable.
   * @param host the host selected for the request
   * @param pool_success indicates if the host can be used for the request.
   */
  virtual void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host,
                                      bool pool_success) PURE;
  /*
   * This will be called if a per-try timeout fails.
   * @param upstream_request inicates which UpstreamRequest which timed out
   */
  virtual void onPerTryTimeout(UpstreamRequest& upstream_request) PURE;

  /*
   * This will be called if a per-try idle timeout fails.
   * @param upstream_request inicates which UpstreamRequest which timed out
   */
  virtual void onPerTryIdleTimeout(UpstreamRequest& upstream_request) PURE;

  /*
   * This will be called if the max stream duration was reached.
   * @param upstream_request inicates which UpstreamRequest which timed out
   */
  virtual void onStreamMaxDurationReached(UpstreamRequest& upstream_request) PURE;

  /*
   * @returns the Router filter's StreamDecoderFilterCallbacks.
   */
  virtual Http::StreamDecoderFilterCallbacks* callbacks() PURE;
  /*
   * @returns the cluster for this stream.
   */
  virtual Upstream::ClusterInfoConstSharedPtr cluster() PURE;

  /*
   * @returns the FilterConfig for this stream
   */
  virtual FilterConfig& config() PURE;

  /*
   * @returns the various timeouts for this stream.
   */
  virtual TimeoutData timeout() PURE;

  /*
   * @returns the dynamic max stream duraration for this stream, if set.
   */
  virtual absl::optional<std::chrono::milliseconds> dynamicMaxStreamDuration() const PURE;

  /*
   * @returns the request headers for the stream.
   */
  virtual Http::RequestHeaderMap* downstreamHeaders() PURE;

  /*
   * @returns the request trailers for the stream.
   */
  virtual Http::RequestTrailerMap* downstreamTrailers() PURE;

  /*
   * @returns true if the downstream response has started.
   */
  virtual bool downstreamResponseStarted() const PURE;

  /*
   * @returns true if end_stream has been sent from the upstream side to the downstream side.
   */
  virtual bool downstreamEndStream() const PURE;

  /*
   * @returns the number of attempts (e.g. retries) performed for this stream.
   */
  virtual uint32_t attemptCount() const PURE;
};

} // namespace Router
} // namespace Envoy
