#include "source/common/router/upstream_request.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/grpc/status.h"
#include "envoy/http/conn_pool.h"
#include "envoy/http/header_map.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/assert.h"
#include "source/common/common/dump_state_utils.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/scope_tracker.h"
#include "source/common/common/utility.h"
#include "source/common/grpc/common.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/network/application_protocol.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/network/upstream_server_name.h"
#include "source/common/network/upstream_subject_alt_names.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/debug_config.h"
#include "source/common/router/router.h"
#include "source/common/stream_info/uint32_accessor_impl.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/common/proxy_protocol/proxy_protocol_header.h"

namespace Envoy {
namespace Router {

// The upstream filter manager class.
class UpstreamFilterManager : public Http::FilterManager {
public:
  UpstreamFilterManager(Http::FilterManagerCallbacks& filter_manager_callbacks,
                        Event::Dispatcher& dispatcher, OptRef<const Network::Connection> connection,
                        uint64_t stream_id, Buffer::BufferMemoryAccountSharedPtr account,
                        bool proxy_100_continue, uint32_t buffer_limit,
                        const Http::FilterChainFactory& filter_chain_factory,
                        UpstreamRequest& request)
      : FilterManager(filter_manager_callbacks, dispatcher, connection, stream_id, account,
                      proxy_100_continue, buffer_limit, filter_chain_factory),
        upstream_request_(request) {}

  StreamInfo::StreamInfo& streamInfo() override {
    return upstream_request_.parent_.callbacks()->streamInfo();
  }
  const StreamInfo::StreamInfo& streamInfo() const override {
    return upstream_request_.parent_.callbacks()->streamInfo();
  }
  // Send local replies via the downstream filter manager.
  // Local replies will not be seen by upstream filters.
  void sendLocalReply(Http::Code code, absl::string_view body,
                      const std::function<void(Http::ResponseHeaderMap& headers)>& modify_headers,
                      const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                      absl::string_view details) override {
    state().decoder_filter_chain_aborted_ = true;
    state().encoder_filter_chain_aborted_ = true;
    state().remote_encode_complete_ = true;
    state().local_complete_ = true;
    // TODO(alyssawilk) this should be done through the router to play well with hedging.
    upstream_request_.parent_.callbacks()->sendLocalReply(code, body, modify_headers, grpc_status,
                                                          details);
  }
  UpstreamRequest& upstream_request_;
};

UpstreamRequest::UpstreamRequest(RouterFilterInterface& parent,
                                 std::unique_ptr<GenericConnPool>&& conn_pool,
                                 bool can_send_early_data, bool can_use_http3)
    : parent_(parent), conn_pool_(std::move(conn_pool)), grpc_rq_success_deferred_(false),
      stream_info_(parent_.callbacks()->dispatcher().timeSource(), nullptr),
      start_time_(parent_.callbacks()->dispatcher().timeSource().monotonicTime()),
      calling_encode_headers_(false), upstream_canary_(false), router_sent_end_stream_(false),
      encode_trailers_(false), retried_(false), awaiting_headers_(true),
      outlier_detection_timeout_recorded_(false),
      create_per_try_timeout_on_request_complete_(false), paused_for_connect_(false),
      reset_stream_(false),
      record_timeout_budget_(parent_.cluster()->timeoutBudgetStats().has_value()),
      cleaned_up_(false), had_upstream_(false),
      allow_upstream_filters_(
          Runtime::runtimeFeatureEnabled("envoy.reloadable_features.allow_upstream_filters")),
      stream_options_({can_send_early_data, can_use_http3}) {
  if (parent_.config().start_child_span_) {
    if (auto tracing_config = parent_.callbacks()->tracingConfig(); tracing_config.has_value()) {
      span_ = parent_.callbacks()->activeSpan().spawnChild(
          tracing_config.value().get(),
          absl::StrCat("router ", parent.cluster()->observabilityName(), " egress"),
          parent.timeSource().systemTime());
      if (parent.attemptCount() != 1) {
        // This is a retry request, add this metadata to span.
        span_->setTag(Tracing::Tags::get().RetryCount, std::to_string(parent.attemptCount() - 1));
      }
    }
  }
  stream_info_.setUpstreamInfo(std::make_shared<StreamInfo::UpstreamInfoImpl>());
  parent_.callbacks()->streamInfo().setUpstreamInfo(stream_info_.upstreamInfo());

  stream_info_.healthCheck(parent_.callbacks()->streamInfo().healthCheck());
  absl::optional<Upstream::ClusterInfoConstSharedPtr> cluster_info =
      parent_.callbacks()->streamInfo().upstreamClusterInfo();
  if (cluster_info.has_value()) {
    stream_info_.setUpstreamClusterInfo(*cluster_info);
  }
  if (!allow_upstream_filters_) {
    return;
  }

  // Set up the upstream filter manager.
  filter_manager_callbacks_ = std::make_unique<UpstreamRequestFilterManagerCallbacks>(*this);
  filter_manager_ = std::make_unique<UpstreamFilterManager>(
      *filter_manager_callbacks_, parent_.callbacks()->dispatcher(),
      parent_.callbacks()->connection(), parent_.callbacks()->streamId(),
      parent_.callbacks()->account(), true, parent_.callbacks()->decoderBufferLimit(),
      *parent_.cluster(), *this);
  // Attempt to create custom cluster-specified filter chain
  bool created = parent_.cluster()->createFilterChain(*filter_manager_,
                                                      /*only_create_if_configured=*/true);
  if (!created) {
    // Attempt to create custom router-specified filter chain.
    created = parent_.config().createFilterChain(*filter_manager_);
  }
  if (!created) {
    // Neither cluster nor router have a custom filter chain; add the default
    // cluster filter chain, which only consists of the codec filter.
    created = parent_.cluster()->createFilterChain(*filter_manager_, false);
  }
  // There will always be a codec filter present, which sets the upstream
  // interface. Fast-fail any tests that don't set up mocks correctly.
  ASSERT(created && upstream_interface_.has_value());
}

UpstreamRequest::~UpstreamRequest() { cleanUp(); }

void UpstreamRequest::cleanUp() {
  if (cleaned_up_) {
    return;
  }
  cleaned_up_ = true;

  if (allow_upstream_filters_) {
    filter_manager_->destroyFilters();
  }

  if (span_ != nullptr) {
    auto tracing_config = parent_.callbacks()->tracingConfig();
    ASSERT(tracing_config.has_value());
    Tracing::HttpTracerUtility::finalizeUpstreamSpan(*span_, stream_info_,
                                                     tracing_config.value().get());
  }

  if (per_try_timeout_ != nullptr) {
    // Allows for testing.
    per_try_timeout_->disableTimer();
  }
  if (per_try_idle_timeout_ != nullptr) {
    // Allows for testing.
    per_try_idle_timeout_->disableTimer();
  }
  if (max_stream_duration_timer_ != nullptr) {
    max_stream_duration_timer_->disableTimer();
  }
  clearRequestEncoder();

  // If desired, fire the per-try histogram when the UpstreamRequest
  // completes.
  if (record_timeout_budget_) {
    Event::Dispatcher& dispatcher = parent_.callbacks()->dispatcher();
    const MonotonicTime end_time = dispatcher.timeSource().monotonicTime();
    const std::chrono::milliseconds response_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time_);
    Upstream::ClusterTimeoutBudgetStatsOptRef tb_stats = parent_.cluster()->timeoutBudgetStats();
    tb_stats->get().upstream_rq_timeout_budget_per_try_percent_used_.recordValue(
        FilterUtility::percentageOfTimeout(response_time, parent_.timeout().per_try_timeout_));
  }

  // Ditto for request/response size histograms.
  Upstream::ClusterRequestResponseSizeStatsOptRef req_resp_stats_opt =
      parent_.cluster()->requestResponseSizeStats();
  if (req_resp_stats_opt.has_value() && parent_.downstreamHeaders()) {
    auto& req_resp_stats = req_resp_stats_opt->get();
    req_resp_stats.upstream_rq_headers_size_.recordValue(parent_.downstreamHeaders()->byteSize());
    req_resp_stats.upstream_rq_body_size_.recordValue(stream_info_.bytesSent());

    if (response_headers_size_.has_value()) {
      req_resp_stats.upstream_rs_headers_size_.recordValue(response_headers_size_.value());
      req_resp_stats.upstream_rs_body_size_.recordValue(stream_info_.bytesReceived());
    }
  }

  stream_info_.onRequestComplete();
  for (const auto& upstream_log : parent_.config().upstream_logs_) {
    upstream_log->log(parent_.downstreamHeaders(), upstream_headers_.get(),
                      upstream_trailers_.get(), stream_info_);
  }

  while (downstream_data_disabled_ != 0) {
    parent_.callbacks()->onDecoderFilterBelowWriteBufferLowWatermark();
    parent_.cluster()->trafficStats().upstream_flow_control_drained_total_.inc();
    --downstream_data_disabled_;
  }
  if (allow_upstream_filters_) {
    // The upstream filter chain callbacks own headers/trailers while they are traversing the filter
    // chain. Make sure to not delete them immediately when the stream ends, as the stream often
    // ends during filter chain processing and it causes use-after-free violations.
    parent_.callbacks()->dispatcher().deferredDelete(std::move(filter_manager_callbacks_));
  }
}

// This is called by the FilterManager when all filters have processed 1xx headers. Forward them
// on to the router.
void UpstreamRequest::decode1xxHeaders(Http::ResponseHeaderMapPtr&& headers) {
  ScopeTrackerScopeState scope(&parent_.callbacks()->scope(), parent_.callbacks()->dispatcher());

  ASSERT(Http::HeaderUtility::isSpecial1xx(*headers));
  addResponseHeadersSize(headers->byteSize());
  parent_.onUpstream1xxHeaders(std::move(headers), *this);
}

// This is called by the FilterManager when all filters have processed headers. Forward them
// on to the router.
void UpstreamRequest::decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) {
  ASSERT(headers.get());
  ENVOY_STREAM_LOG(trace, "upstream response headers:\n{}", *parent_.callbacks(), *headers);
  ScopeTrackerScopeState scope(&parent_.callbacks()->scope(), parent_.callbacks()->dispatcher());

  resetPerTryIdleTimer();

  addResponseHeadersSize(headers->byteSize());

  // We drop unsupported 1xx on the floor here. 101 upgrade headers need to be passed to the client
  // as part of the final response. Most 1xx headers are handled in onUpstream1xxHeaders.
  //
  // We could in principle handle other headers here, but this might result in the double invocation
  // of decodeHeaders() (once for informational, again for non-informational), which is likely an
  // easy to miss corner case in the filter and HCM contract.
  //
  // This filtering is done early in upstream request, unlike 100 coalescing which is performed in
  // the router filter, since the filtering only depends on the state of a single upstream, and we
  // don't want to confuse accounting such as onFirstUpstreamRxByteReceived() with informational
  // headers.
  const uint64_t response_code = Http::Utility::getResponseStatus(*headers);
  if (Http::CodeUtility::is1xx(response_code) &&
      response_code != enumToInt(Http::Code::SwitchingProtocols)) {
    return;
  }

  if (!allow_upstream_filters_) {
    // TODO(rodaine): This is actually measuring after the headers are parsed and not the first
    // byte.
    upstreamTiming().onFirstUpstreamRxByteReceived(parent_.callbacks()->dispatcher().timeSource());
    maybeEndDecode(end_stream);
  }

  awaiting_headers_ = false;
  if (span_ != nullptr) {
    Tracing::HttpTracerUtility::onUpstreamResponseHeaders(*span_, headers.get());
  }
  if (!parent_.config().upstream_logs_.empty()) {
    upstream_headers_ = Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*headers);
  }
  stream_info_.response_code_ = static_cast<uint32_t>(response_code);

  if (paused_for_connect_ && response_code == 200) {
    encodeBodyAndTrailers();
    paused_for_connect_ = false;
  }

  ASSERT(headers.get());
  parent_.onUpstreamHeaders(response_code, std::move(headers), *this, end_stream);
}

void UpstreamRequest::decodeData(Buffer::Instance& data, bool end_stream) {
  ScopeTrackerScopeState scope(&parent_.callbacks()->scope(), parent_.callbacks()->dispatcher());

  resetPerTryIdleTimer();
  if (!allow_upstream_filters_) {
    maybeEndDecode(end_stream);
  }
  stream_info_.addBytesReceived(data.length());
  parent_.onUpstreamData(data, *this, end_stream);
}

void UpstreamRequest::decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) {
  ENVOY_STREAM_LOG(trace, "upstream response trailers:\n{}", *parent_.callbacks(), *trailers);
  ScopeTrackerScopeState scope(&parent_.callbacks()->scope(), parent_.callbacks()->dispatcher());

  if (!allow_upstream_filters_) {
    maybeEndDecode(true);
  }
  if (span_ != nullptr) {
    Tracing::HttpTracerUtility::onUpstreamResponseTrailers(*span_, trailers.get());
  }
  if (!parent_.config().upstream_logs_.empty()) {
    upstream_trailers_ = Http::createHeaderMap<Http::ResponseTrailerMapImpl>(*trailers);
  }
  parent_.onUpstreamTrailers(std::move(trailers), *this);
}

void UpstreamRequest::dumpState(std::ostream& os, int indent_level) const {
  const char* spaces = spacesForLevel(indent_level);
  os << spaces << "UpstreamRequest " << this << "\n";
  if (connection()) {
    const auto addressProvider = connection()->connectionInfoProviderSharedPtr();
    DUMP_DETAILS(addressProvider);
  }
  const Http::RequestHeaderMap* request_headers = parent_.downstreamHeaders();
  DUMP_DETAILS(request_headers);
  if (filter_manager_) {
    filter_manager_->dumpState(os, indent_level);
  }
}

const Route& UpstreamRequest::route() const { return *parent_.route(); }

OptRef<const Network::Connection> UpstreamRequest::connection() const {
  return parent_.callbacks()->connection();
}

void UpstreamRequest::decodeMetadata(Http::MetadataMapPtr&& metadata_map) {
  parent_.onUpstreamMetadata(std::move(metadata_map));
}

void UpstreamRequest::maybeEndDecode(bool end_stream) {
  if (end_stream) {
    upstreamTiming().onLastUpstreamRxByteReceived(parent_.callbacks()->dispatcher().timeSource());
  }
}

void UpstreamRequest::onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) {
  StreamInfo::UpstreamInfo& upstream_info = *streamInfo().upstreamInfo();
  upstream_info.setUpstreamHost(host);
  upstream_host_ = host;
  parent_.onUpstreamHostSelected(host);
}

void UpstreamRequest::acceptHeadersFromRouter(bool end_stream) {
  ASSERT(!router_sent_end_stream_);
  router_sent_end_stream_ = end_stream;

  // Kick off creation of the upstream connection immediately upon receiving headers.
  // In future it may be possible for upstream filters to delay this, or influence connection
  // creation but for now optimize for minimal latency and fetch the connection
  // as soon as possible.
  conn_pool_->newStream(this);
  if (!allow_upstream_filters_) {
    return;
  }

  auto* headers = parent_.downstreamHeaders();

  // Make sure that when we are forwarding CONNECT payload we do not do so until
  // the upstream has accepted the CONNECT request.
  if (headers->getMethodValue() == Http::Headers::get().MethodValues.Connect) {
    paused_for_connect_ = true;
  }

  filter_manager_->requestHeadersInitialized();
  filter_manager_->streamInfo().setRequestHeaders(*parent_.downstreamHeaders());
  filter_manager_->decodeHeaders(*parent_.downstreamHeaders(), end_stream);
}

void UpstreamRequest::acceptDataFromRouterOld(Buffer::Instance& data, bool end_stream) {
  if (!upstream_ || paused_for_connect_) {
    ENVOY_STREAM_LOG(trace, "buffering {} bytes", *parent_.callbacks(), data.length());
    if (!buffered_request_body_) {
      buffered_request_body_ = parent_.callbacks()->dispatcher().getWatermarkFactory().createBuffer(
          [this]() -> void { this->enableDataFromDownstreamForFlowControl(); },
          [this]() -> void { this->disableDataFromDownstreamForFlowControl(); },
          []() -> void { /* TODO(adisuissa): Handle overflow watermark */ });
      buffered_request_body_->setWatermarks(parent_.callbacks()->decoderBufferLimit());
    }

    buffered_request_body_->move(data);
  } else {
    ASSERT(downstream_metadata_map_vector_.empty());

    ENVOY_STREAM_LOG(trace, "proxying {} bytes", *parent_.callbacks(), data.length());
    stream_info_.addBytesSent(data.length());
    upstream_->encodeData(data, end_stream);
    if (end_stream) {
      upstreamTiming().onLastUpstreamTxByteSent(parent_.callbacks()->dispatcher().timeSource());
    }
  }
}

void UpstreamRequest::acceptDataFromRouter(Buffer::Instance& data, bool end_stream) {
  ASSERT(!router_sent_end_stream_);
  router_sent_end_stream_ = end_stream;

  if (!allow_upstream_filters_) {
    acceptDataFromRouterOld(data, end_stream);
    return;
  }

  filter_manager_->decodeData(data, end_stream);
}

void UpstreamRequest::acceptTrailersFromRouterOld(Http::RequestTrailerMap& trailers) {
  router_sent_end_stream_ = true;
  encode_trailers_ = true;

  if (!upstream_) {
    ENVOY_STREAM_LOG(trace, "buffering trailers", *parent_.callbacks());
  } else {
    ASSERT(downstream_metadata_map_vector_.empty());

    ENVOY_STREAM_LOG(trace, "proxying trailers", *parent_.callbacks());
    upstream_->encodeTrailers(trailers);
    upstreamTiming().onLastUpstreamTxByteSent(parent_.callbacks()->dispatcher().timeSource());
  }
}

void UpstreamRequest::acceptTrailersFromRouter(Http::RequestTrailerMap& trailers) {
  if (!allow_upstream_filters_) {
    acceptTrailersFromRouterOld(trailers);
    return;
  }

  ASSERT(!router_sent_end_stream_);
  router_sent_end_stream_ = true;
  encode_trailers_ = true;

  filter_manager_->decodeTrailers(trailers);
}

void UpstreamRequest::acceptMetadataFromRouterOld(Http::MetadataMapPtr&& metadata_map_ptr) {
  if (!upstream_) {
    ENVOY_STREAM_LOG(trace, "upstream_ not ready. Store metadata_map to encode later: {}",
                     *parent_.callbacks(), *metadata_map_ptr);
    downstream_metadata_map_vector_.emplace_back(std::move(metadata_map_ptr));
  } else {
    ENVOY_STREAM_LOG(trace, "Encode metadata: {}", *parent_.callbacks(), *metadata_map_ptr);
    Http::MetadataMapVector metadata_map_vector;
    metadata_map_vector.emplace_back(std::move(metadata_map_ptr));
    upstream_->encodeMetadata(metadata_map_vector);
  }
}

void UpstreamRequest::acceptMetadataFromRouter(Http::MetadataMapPtr&& metadata_map_ptr) {
  if (!allow_upstream_filters_) {
    acceptMetadataFromRouterOld(std::move(metadata_map_ptr));
    return;
  }

  filter_manager_->decodeMetadata(*metadata_map_ptr);
}

void UpstreamRequest::onResetStream(Http::StreamResetReason reason,
                                    absl::string_view transport_failure_reason) {
  ScopeTrackerScopeState scope(&parent_.callbacks()->scope(), parent_.callbacks()->dispatcher());

  if (span_ != nullptr) {
    // Add tags about reset.
    span_->setTag(Tracing::Tags::get().Error, Tracing::Tags::get().True);
    span_->setTag(Tracing::Tags::get().ErrorReason, Http::Utility::resetReasonToString(reason));
  }
  clearRequestEncoder();
  awaiting_headers_ = false;
  if (!calling_encode_headers_) {
    stream_info_.setResponseFlag(Filter::streamResetReasonToResponseFlag(reason));
    parent_.onUpstreamReset(reason, transport_failure_reason, *this);
  } else {
    deferred_reset_reason_ = reason;
  }
}

void UpstreamRequest::resetStream() {
  if (conn_pool_->cancelAnyPendingStream()) {
    ENVOY_STREAM_LOG(debug, "canceled pool request", *parent_.callbacks());
    ASSERT(!upstream_);
  }

  // Don't reset the stream if we're already done with it.
  if (upstreamTiming().last_upstream_tx_byte_sent_.has_value() &&
      upstreamTiming().last_upstream_rx_byte_received_.has_value()) {
    return;
  }

  if (span_ != nullptr) {
    // Add tags about the cancellation.
    span_->setTag(Tracing::Tags::get().Canceled, Tracing::Tags::get().True);
  }

  if (upstream_) {
    ENVOY_STREAM_LOG(debug, "resetting pool request", *parent_.callbacks());
    upstream_->resetStream();
    clearRequestEncoder();
  }
  reset_stream_ = true;
}

void UpstreamRequest::resetPerTryIdleTimer() {
  if (per_try_idle_timeout_ != nullptr) {
    per_try_idle_timeout_->enableTimer(parent_.timeout().per_try_idle_timeout_);
  }
}

void UpstreamRequest::setupPerTryTimeout() {
  ASSERT(!per_try_timeout_);
  if (parent_.timeout().per_try_timeout_.count() > 0) {
    per_try_timeout_ =
        parent_.callbacks()->dispatcher().createTimer([this]() -> void { onPerTryTimeout(); });
    per_try_timeout_->enableTimer(parent_.timeout().per_try_timeout_);
  }

  ASSERT(!per_try_idle_timeout_);
  if (parent_.timeout().per_try_idle_timeout_.count() > 0) {
    per_try_idle_timeout_ =
        parent_.callbacks()->dispatcher().createTimer([this]() -> void { onPerTryIdleTimeout(); });
    resetPerTryIdleTimer();
  }
}

void UpstreamRequest::onPerTryIdleTimeout() {
  ENVOY_STREAM_LOG(debug, "upstream per try idle timeout", *parent_.callbacks());
  stream_info_.setResponseFlag(StreamInfo::ResponseFlag::StreamIdleTimeout);
  parent_.onPerTryIdleTimeout(*this);
}

void UpstreamRequest::onPerTryTimeout() {
  // If we've sent anything downstream, ignore the per try timeout and let the response continue
  // up to the global timeout
  if (!parent_.downstreamResponseStarted()) {
    ENVOY_STREAM_LOG(debug, "upstream per try timeout", *parent_.callbacks());

    stream_info_.setResponseFlag(StreamInfo::ResponseFlag::UpstreamRequestTimeout);
    parent_.onPerTryTimeout(*this);
  } else {
    ENVOY_STREAM_LOG(debug,
                     "ignored upstream per try timeout due to already started downstream response",
                     *parent_.callbacks());
  }
}

void UpstreamRequest::onPoolFailure(ConnectionPool::PoolFailureReason reason,
                                    absl::string_view transport_failure_reason,
                                    Upstream::HostDescriptionConstSharedPtr host) {
  Http::StreamResetReason reset_reason = Http::StreamResetReason::ConnectionFailure;
  switch (reason) {
  case ConnectionPool::PoolFailureReason::Overflow:
    reset_reason = Http::StreamResetReason::Overflow;
    break;
  case ConnectionPool::PoolFailureReason::RemoteConnectionFailure:
    FALLTHRU;
  case ConnectionPool::PoolFailureReason::LocalConnectionFailure:
    reset_reason = Http::StreamResetReason::ConnectionFailure;
    break;
  case ConnectionPool::PoolFailureReason::Timeout:
    reset_reason = Http::StreamResetReason::ConnectionFailure;
  }

  stream_info_.upstreamInfo()->setUpstreamTransportFailureReason(transport_failure_reason);

  // Mimic an upstream reset.
  onUpstreamHostSelected(host);
  onResetStream(reset_reason, transport_failure_reason);
}

void UpstreamRequest::onPoolReady(std::unique_ptr<GenericUpstream>&& upstream,
                                  Upstream::HostDescriptionConstSharedPtr host,
                                  const Network::ConnectionInfoProvider& address_provider,
                                  StreamInfo::StreamInfo& info,
                                  absl::optional<Http::Protocol> protocol) {
  // This may be called under an existing ScopeTrackerScopeState but it will unwind correctly.
  ScopeTrackerScopeState scope(&parent_.callbacks()->scope(), parent_.callbacks()->dispatcher());
  ENVOY_STREAM_LOG(debug, "pool ready", *parent_.callbacks());
  upstream_ = std::move(upstream);
  had_upstream_ = true;
  // Have the upstream use the account of the downstream.
  upstream_->setAccount(parent_.callbacks()->account());

  if (parent_.requestVcluster()) {
    // The cluster increases its upstream_rq_total_ counter right before firing this onPoolReady
    // callback. Hence, the upstream request increases the virtual cluster's upstream_rq_total_ stat
    // here.
    parent_.requestVcluster()->stats().upstream_rq_total_.inc();
  }
  if (parent_.routeStatsContext().has_value()) {
    // The cluster increases its upstream_rq_total_ counter right before firing this onPoolReady
    // callback. Hence, the upstream request increases the route level upstream_rq_total_ stat
    // here.
    parent_.routeStatsContext()->stats().upstream_rq_total_.inc();
  }

  host->outlierDetector().putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess);

  onUpstreamHostSelected(host);

  if (protocol) {
    stream_info_.protocol(protocol.value());
  } else if (allow_upstream_filters_) {
    // We only pause for CONNECT for HTTP upstreams. If this is a TCP upstream, unpause.
    paused_for_connect_ = false;
  }

  StreamInfo::UpstreamInfo& upstream_info = *stream_info_.upstreamInfo();
  if (info.upstreamInfo()) {
    auto& upstream_timing = info.upstreamInfo()->upstreamTiming();
    upstreamTiming().upstream_connect_start_ = upstream_timing.upstream_connect_start_;
    upstreamTiming().upstream_connect_complete_ = upstream_timing.upstream_connect_complete_;
    upstreamTiming().upstream_handshake_complete_ = upstream_timing.upstream_handshake_complete_;
    upstream_info.setUpstreamNumStreams(info.upstreamInfo()->upstreamNumStreams());
  }

  // Upstream filters might have already created/set a filter state.
  const StreamInfo::FilterStateSharedPtr& filter_state = info.filterState();
  if (!filter_state) {
    upstream_info.setUpstreamFilterState(
        std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Request));
  } else {
    upstream_info.setUpstreamFilterState(filter_state);
  }
  upstream_info.setUpstreamLocalAddress(address_provider.localAddress());
  upstream_info.setUpstreamRemoteAddress(address_provider.remoteAddress());
  upstream_info.setUpstreamSslConnection(info.downstreamAddressProvider().sslConnection());

  if (info.downstreamAddressProvider().connectionID().has_value()) {
    upstream_info.setUpstreamConnectionId(info.downstreamAddressProvider().connectionID().value());
  }

  if (info.downstreamAddressProvider().interfaceName().has_value()) {
    upstream_info.setUpstreamInterfaceName(
        info.downstreamAddressProvider().interfaceName().value());
  }

  stream_info_.setUpstreamBytesMeter(upstream_->bytesMeter());
  StreamInfo::StreamInfo::syncUpstreamAndDownstreamBytesMeter(parent_.callbacks()->streamInfo(),
                                                              stream_info_);
  if (protocol) {
    upstream_info.setUpstreamProtocol(protocol.value());
  }

  if (parent_.downstreamEndStream()) {
    setupPerTryTimeout();
  } else {
    create_per_try_timeout_on_request_complete_ = true;
  }

  // Make sure the connection manager will inform the downstream watermark manager when the
  // downstream buffers are overrun. This may result in immediate watermark callbacks referencing
  // the encoder.
  parent_.callbacks()->addDownstreamWatermarkCallbacks(downstream_watermark_manager_);

  absl::optional<std::chrono::milliseconds> max_stream_duration;
  if (parent_.dynamicMaxStreamDuration().has_value()) {
    max_stream_duration = parent_.dynamicMaxStreamDuration().value();
  } else if (upstream_host_->cluster().commonHttpProtocolOptions().has_max_stream_duration()) {
    max_stream_duration = std::chrono::milliseconds(DurationUtil::durationToMilliseconds(
        upstream_host_->cluster().commonHttpProtocolOptions().max_stream_duration()));
  }
  if (max_stream_duration.has_value() && max_stream_duration->count()) {
    max_stream_duration_timer_ = parent_.callbacks()->dispatcher().createTimer(
        [this]() -> void { onStreamMaxDurationReached(); });
    max_stream_duration_timer_->enableTimer(*max_stream_duration);
  }

  const auto* route_entry = parent_.route()->routeEntry();
  if (route_entry->autoHostRewrite() && !host->hostname().empty()) {
    Http::Utility::updateAuthority(*parent_.downstreamHeaders(), host->hostname(),
                                   route_entry->appendXfh());
  }

  if (span_ != nullptr) {
    span_->injectContext(*parent_.downstreamHeaders(), host);
  } else {
    // No independent child span for current upstream request then inject the parent span's tracing
    // context into the request headers.
    // The injectContext() of the parent span may be called repeatedly when the request is retried.
    parent_.callbacks()->activeSpan().injectContext(*parent_.downstreamHeaders(), host);
  }

  stream_info_.setRequestHeaders(*parent_.downstreamHeaders());

  for (auto* callback : upstream_callbacks_) {
    callback->onUpstreamConnectionEstablished();
    return;
  }

  auto* headers = parent_.downstreamHeaders();
  calling_encode_headers_ = true;
  upstreamTiming().onFirstUpstreamTxByteSent(parent_.callbacks()->dispatcher().timeSource());

  // Make sure that when we are forwarding CONNECT payload we do not do so until
  // the upstream has accepted the CONNECT request.
  if (protocol.has_value() &&
      headers->getMethodValue() == Http::Headers::get().MethodValues.Connect) {
    paused_for_connect_ = true;
  }

  const Http::Status status =
      upstream_->encodeHeaders(*parent_.downstreamHeaders(), shouldSendEndStream());
  calling_encode_headers_ = false;

  if (!status.ok()) {
    // It is possible that encodeHeaders() fails. This can happen if filters or other extensions
    // erroneously remove required headers.
    stream_info_.setResponseFlag(StreamInfo::ResponseFlag::DownstreamProtocolError);
    const std::string details =
        absl::StrCat(StreamInfo::ResponseCodeDetails::get().FilterRemovedRequiredRequestHeaders,
                     "{", StringUtil::replaceAllEmptySpace(status.message()), "}");
    parent_.callbacks()->sendLocalReply(Http::Code::ServiceUnavailable, status.message(), nullptr,
                                        absl::nullopt, details);
    return;
  }

  if (!paused_for_connect_) {
    encodeBodyAndTrailers();
  }
}

void UpstreamRequest::encodeBodyAndTrailers() {
  if (allow_upstream_filters_) {
    return;
  }
  // It is possible to get reset in the middle of an encodeHeaders() call. This happens for
  // example in the HTTP/2 codec if the frame cannot be encoded for some reason. This should never
  // happen but it's unclear if we have covered all cases so protect against it and test for it.
  // One specific example of a case where this happens is if we try to encode a total header size
  // that is too big in HTTP/2 (64K currently).
  if (deferred_reset_reason_) {
    onResetStream(deferred_reset_reason_.value(), absl::string_view());
  } else {
    // Encode metadata after headers and before any other frame type.
    if (!downstream_metadata_map_vector_.empty()) {
      ENVOY_STREAM_LOG(debug, "Send metadata onPoolReady. {}", *parent_.callbacks(),
                       downstream_metadata_map_vector_);
      upstream_->encodeMetadata(downstream_metadata_map_vector_);
      downstream_metadata_map_vector_.clear();
    }

    if (buffered_request_body_) {
      stream_info_.addBytesSent(buffered_request_body_->length());
      upstream_->encodeData(*buffered_request_body_, router_sent_end_stream_ && !encode_trailers_);
    }

    if (encode_trailers_) {
      upstream_->encodeTrailers(*parent_.downstreamTrailers());
    }

    if (router_sent_end_stream_) {
      upstreamTiming().onLastUpstreamTxByteSent(parent_.callbacks()->dispatcher().timeSource());
    }
  }
}

UpstreamToDownstream& UpstreamRequest::upstreamToDownstream() {
  if (allow_upstream_filters_) {
    return *upstream_interface_;
  }
  return *this;
}

void UpstreamRequest::onStreamMaxDurationReached() {
  upstream_host_->cluster().trafficStats().upstream_rq_max_duration_reached_.inc();

  // The upstream had closed then try to retry along with retry policy.
  parent_.onStreamMaxDurationReached(*this);
}

void UpstreamRequest::clearRequestEncoder() {
  // Before clearing the encoder, unsubscribe from callbacks.
  if (upstream_) {
    parent_.callbacks()->removeDownstreamWatermarkCallbacks(downstream_watermark_manager_);
  }
  upstream_.reset();
}

void UpstreamRequest::DownstreamWatermarkManager::onAboveWriteBufferHighWatermark() {
  ASSERT(parent_.upstream_);

  // There are two states we should get this callback in: 1) the watermark was
  // hit due to writes from a different filter instance over a shared
  // downstream connection, or 2) the watermark was hit due to THIS filter
  // instance writing back the "winning" upstream request. In either case we
  // can disable reads from upstream.
  ASSERT(!parent_.parent_.finalUpstreamRequest() ||
         &parent_ == parent_.parent_.finalUpstreamRequest());
  // The downstream connection is overrun. Pause reads from upstream.
  // If there are multiple calls to readDisable either the codec (H2) or the underlying
  // Network::Connection (H1) will handle reference counting.
  parent_.parent_.cluster()->trafficStats().upstream_flow_control_paused_reading_total_.inc();
  parent_.upstream_->readDisable(true);
}

void UpstreamRequest::DownstreamWatermarkManager::onBelowWriteBufferLowWatermark() {
  ASSERT(parent_.upstream_);

  // One source of connection blockage has buffer available. Pass this on to the stream, which
  // will resume reads if this was the last remaining high watermark.
  parent_.parent_.cluster()->trafficStats().upstream_flow_control_resumed_reading_total_.inc();
  parent_.upstream_->readDisable(false);
}

void UpstreamRequest::disableDataFromDownstreamForFlowControl() {
  // If there is only one upstream request, we can be assured that
  // disabling reads will not slow down other upstream requests. If we've
  // already seen the full downstream request (downstream_end_stream_) then
  // disabling reads is a noop.
  // This assert condition must be true because
  // parent_.upstreamRequests().size() can only be greater than 1 in the
  // case of a per-try-timeout with hedge_on_per_try_timeout enabled, and
  // the per try timeout timer is started only after downstream_end_stream_
  // is true.
  ASSERT(parent_.upstreamRequests().size() == 1 || parent_.downstreamEndStream());
  parent_.cluster()->trafficStats().upstream_flow_control_backed_up_total_.inc();
  parent_.callbacks()->onDecoderFilterAboveWriteBufferHighWatermark();
  ++downstream_data_disabled_;
}

void UpstreamRequest::enableDataFromDownstreamForFlowControl() {
  // If there is only one upstream request, we can be assured that
  // disabling reads will not overflow any write buffers in other upstream
  // requests. If we've already seen the full downstream request
  // (downstream_end_stream_) then enabling reads is a noop.
  // This assert condition must be true because
  // parent_.upstreamRequests().size() can only be greater than 1 in the
  // case of a per-try-timeout with hedge_on_per_try_timeout enabled, and
  // the per try timeout timer is started only after downstream_end_stream_
  // is true.
  ASSERT(parent_.upstreamRequests().size() == 1 || parent_.downstreamEndStream());
  parent_.cluster()->trafficStats().upstream_flow_control_drained_total_.inc();
  parent_.callbacks()->onDecoderFilterBelowWriteBufferLowWatermark();
  ASSERT(downstream_data_disabled_ != 0);
  if (downstream_data_disabled_ > 0) {
    --downstream_data_disabled_;
  }
}

Http::RequestHeaderMapOptRef UpstreamRequestFilterManagerCallbacks::requestHeaders() {
  return {*upstream_request_.parent_.downstreamHeaders()};
}

Http::RequestTrailerMapOptRef UpstreamRequestFilterManagerCallbacks::requestTrailers() {
  if (upstream_request_.parent_.downstreamTrailers()) {
    return {*upstream_request_.parent_.downstreamTrailers()};
  }
  if (trailers_) {
    return {*trailers_};
  }
  return {};
}

const ScopeTrackedObject& UpstreamRequestFilterManagerCallbacks::scope() {
  return upstream_request_.parent_.callbacks()->scope();
}

OptRef<const Tracing::Config> UpstreamRequestFilterManagerCallbacks::tracingConfig() const {
  return upstream_request_.parent_.callbacks()->tracingConfig();
}

Tracing::Span& UpstreamRequestFilterManagerCallbacks::activeSpan() {
  return upstream_request_.parent_.callbacks()->activeSpan();
}

void UpstreamRequestFilterManagerCallbacks::resetStream(
    Http::StreamResetReason reset_reason, absl::string_view transport_failure_reason) {
  // The filter manager needs to disambiguate between a filter-driven reset,
  // which should force reset the stream, and a codec driven reset, which should
  // tell the router the stream reset, and let the router make the decision to
  // send a local reply, or retry the stream.
  if (reset_reason == Http::StreamResetReason::LocalReset &&
      transport_failure_reason != "codec_error") {
    upstream_request_.parent_.callbacks()->resetStream();
    return;
  }
  return upstream_request_.onResetStream(reset_reason, transport_failure_reason);
}

Upstream::ClusterInfoConstSharedPtr UpstreamRequestFilterManagerCallbacks::clusterInfo() {
  return upstream_request_.parent_.callbacks()->clusterInfo();
}

Http::Http1StreamEncoderOptionsOptRef
UpstreamRequestFilterManagerCallbacks::http1StreamEncoderOptions() {
  return upstream_request_.parent_.callbacks()->http1StreamEncoderOptions();
}

} // namespace Router
} // namespace Envoy
