#pragma once

#include <cstdint>

#include "envoy/extensions/filters/network/generic_proxy/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/generic_proxy/router/v3/router.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/server/factory_context.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/linked_object.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/upstream/load_balancer_context_base.h"
#include "source/extensions/filters/network/generic_proxy/interface/codec.h"
#include "source/extensions/filters/network/generic_proxy/interface/filter.h"
#include "source/extensions/filters/network/generic_proxy/interface/stream.h"
#include "source/extensions/filters/network/generic_proxy/router/upstream.h"

#include "quiche/common/quiche_linked_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Router {

/**
 * Stream reset reasons.
 */
enum class StreamResetReason : uint32_t {
  LocalReset,
  // If the stream was locally reset by a connection pool due to an initial connection failure.
  ConnectionFailure,
  // If the stream was locally reset due to connection termination.
  ConnectionTermination,
  // The stream was reset because of a resource overflow.
  Overflow,
  // Protocol error.
  ProtocolError,

  LastReason = ProtocolError,
};

class RouterFilter;

class UpstreamRequest : public UpstreamRequestCallbacks,
                        public LinkedObject<UpstreamRequest>,
                        public EncodingContext,
                        Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  UpstreamRequest(RouterFilter& parent, FrameFlags header_frame_flags,
                  GenericUpstreamSharedPtr generic_upstream);

  void startStream();
  void resetStream(StreamResetReason reason, absl::string_view reason_detail);
  void clearStream(bool close_connection);

  // Called when the stream has been reset or completed.
  void deferredDelete();

  // UpstreamRequestCallbacks
  void onUpstreamFailure(ConnectionPool::PoolFailureReason reason,
                         absl::string_view transport_failure_reason) override;
  void onUpstreamSuccess() override;
  void onConnectionClose(Network::ConnectionEvent event) override;
  void onDecodingSuccess(ResponseHeaderFramePtr response_header_frame,
                         absl::optional<StartTime> start_time = {}) override;
  void onDecodingSuccess(ResponseCommonFramePtr response_common_frame) override;
  void onDecodingFailure(absl::string_view reason) override;

  // EncodingContext
  OptRef<const RouteEntry> routeEntry() const override;

  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host);
  void onUpstreamConnectionReady();
  void encodeBufferToUpstream(Buffer::Instance& buffer);

  void sendHeaderFrameToUpstream();
  void sendCommonFrameToUpstream();
  bool sendFrameToUpstream(const StreamFrame& frame, bool header_frame);

  void onUpstreamResponseComplete(bool drain_close);

  RouterFilter& parent_;
  GenericUpstreamSharedPtr generic_upstream_;

  StreamInfo::StreamInfoImpl stream_info_;
  std::shared_ptr<StreamInfo::UpstreamInfoImpl> upstream_info_;
  OptRef<const Tracing::Config> tracing_config_;
  Tracing::SpanPtr span_;

  absl::optional<MonotonicTime> connecting_start_time_;

  const uint64_t stream_id_{};
  const bool expects_response_{};

  // One of these flags should be set to true when the request is complete.
  bool reset_or_response_complete_{};

  bool request_stream_header_sent_{};
  bool response_stream_header_received_{};
};
using UpstreamRequestPtr = std::unique_ptr<UpstreamRequest>;

class RouterConfig {
public:
  RouterConfig(const envoy::extensions::filters::network::generic_proxy::router::v3::Router& config)
      : bind_upstream_connection_(config.bind_upstream_connection()) {}

  bool bindUpstreamConnection() const { return bind_upstream_connection_; }

private:
  const bool bind_upstream_connection_{};
};
using RouterConfigSharedPtr = std::shared_ptr<RouterConfig>;

class RouterFilter : public DecoderFilter,
                     public Upstream::LoadBalancerContextBase,
                     public RequestFramesHandler,
                     Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  RouterFilter(RouterConfigSharedPtr config, Server::Configuration::FactoryContext& context,
               GenericUpstreamFactory* upstream_factory = nullptr)
      : config_(std::move(config)), generic_upstream_factory_(upstream_factory),
        cluster_manager_(context.serverFactoryContext().clusterManager()),
        time_source_(context.serverFactoryContext().timeSource()) {
    if (generic_upstream_factory_ == nullptr) {
      generic_upstream_factory_ = &DefaultGenericUpstreamFactory::get();
    }
  }

  // DecoderFilter
  void onDestroy() override;

  void setDecoderFilterCallbacks(DecoderFilterCallback& callbacks) override {
    callbacks_ = &callbacks;
    callbacks_->setRequestFramesHandler(this);
  }
  HeaderFilterStatus decodeHeaderFrame(RequestHeaderFrame& request) override;
  CommonFilterStatus decodeCommonFrame(RequestCommonFrame&) override {
    return CommonFilterStatus::StopIteration;
  }

  void onResponseHeaderFrame(ResponseHeaderFramePtr response_header_frame);
  void onResponseCommonFrame(ResponseCommonFramePtr response_common_frame);
  void completeDirectly();

  void onUpstreamRequestReset(UpstreamRequest& upstream_request, StreamResetReason reason,
                              absl::string_view reason_detail);
  void onTimeout();

  void setRouteEntry(const RouteEntry* route_entry) {
    route_entry_ = route_entry;
    max_retries_ = route_entry_ ? route_entry->retryPolicy().numRetries() : 1;
  }

  size_t upstreamRequestsSize() { return upstream_request_ != nullptr ? 1 : 0; }

  // Upstream::LoadBalancerContextBase
  const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() override;
  const Network::Connection* downstreamConnection() const override;

  // RequestFramesHandler
  void onRequestCommonFrame(RequestCommonFramePtr frame) override;

private:
  friend class UpstreamRequest;
  friend class UpstreamManagerImpl;

  void kickOffNewUpstreamRequest();
  void completeAndSendLocalReply(absl::Status status, absl::string_view details,
                                 absl::optional<StreamInfo::CoreResponseFlag> flag = {});

  // Clean up all the upstream requests, statuses and timers. All further events will be
  // ignored.
  void onFilterComplete();

  // Check if all request frames are ready and fire the timeout timer if necessary.
  void mayRequestStreamEnd(bool stream_end_stream);

  // Check if retry is allowed.
  bool couldRetry(absl::optional<StreamResetReason> reason) {
    // If the upstream connection is bound to the downstream connection and resetting happens,
    // we should not retry the request because the downstream connection will be closed.
    if (reason.has_value() && config_->bindUpstreamConnection()) {
      return false;
    }
    return num_retries_ < max_retries_;
  }

  // Set this flag if the downstream request is cancelled, reset or completed, or the upstream
  // response is completely received to tell the filter to ignore all further events.
  bool filter_complete_{};

  const RouteEntry* route_entry_{};
  uint32_t num_retries_{0};
  uint32_t max_retries_{1};

  Upstream::ClusterInfoConstSharedPtr cluster_;
  Request* request_stream_{};
  std::list<RequestCommonFramePtr> request_stream_frames_;
  bool request_stream_end_{};

  Envoy::Router::MetadataMatchCriteriaConstPtr metadata_match_;

  // Only allow one upstream request at a time. This is a simplification of the code that may be
  // changed in the future.
  UpstreamRequestPtr upstream_request_;
  Envoy::Event::TimerPtr timeout_timer_;

  DecoderFilterCallback* callbacks_{};

  RouterConfigSharedPtr config_;
  const GenericUpstreamFactory* generic_upstream_factory_{};

  Upstream::ClusterManager& cluster_manager_;
  TimeSource& time_source_;
};

} // namespace Router
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
