#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.h"
#include "envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.validate.h"
#include "envoy/extensions/filters/network/generic_proxy/v3/route.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/timespan.h"
#include "envoy/tracing/tracer_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"
#include "source/common/stats/timespan_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/tracing/tracer_config_impl.h"
#include "source/common/tracing/tracer_impl.h"
#include "source/extensions/filters/network/generic_proxy/interface/filter.h"
#include "source/extensions/filters/network/generic_proxy/interface/stream.h"
#include "source/extensions/filters/network/generic_proxy/proxy_config.h"
#include "source/extensions/filters/network/generic_proxy/rds.h"
#include "source/extensions/filters/network/generic_proxy/rds_impl.h"
#include "source/extensions/filters/network/generic_proxy/route.h"
#include "source/extensions/filters/network/generic_proxy/route_impl.h"
#include "source/extensions/filters/network/generic_proxy/stats.h"
#include "source/extensions/filters/network/generic_proxy/tracing.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

using ProxyConfig = envoy::extensions::filters::network::generic_proxy::v3::GenericProxy;
using RouteConfiguration =
    envoy::extensions::filters::network::generic_proxy::v3::RouteConfiguration;

class Filter;
class ActiveStream;

struct NamedFilterFactoryCb {
  std::string config_name_;
  FilterFactoryCb callback_;
};

enum class DownstreamStreamResetReason : uint32_t {
  // The stream was reset because of the connection was closed.
  ConnectionTermination,
  // The stream was reset because of the connection was closed locally.
  LocalConnectionTermination,
  // Protocol error.
  ProtocolError,
};

class FilterConfigImpl : public FilterConfig {
public:
  FilterConfigImpl(const std::string& stat_prefix, CodecFactoryPtr codec,
                   Rds::RouteConfigProviderSharedPtr route_config_provider,
                   std::vector<NamedFilterFactoryCb> factories, Tracing::TracerSharedPtr tracer,
                   Tracing::ConnectionManagerTracingConfigPtr tracing_config,
                   std::vector<AccessLogInstanceSharedPtr>&& access_logs,
                   const CodeOrFlags& code_or_flags,
                   Envoy::Server::Configuration::FactoryContext& context)
      : stat_prefix_(stat_prefix),
        stats_(GenericFilterStats::generateStats(stat_prefix_, context.scope())),
        code_or_flags_(code_or_flags), codec_factory_(std::move(codec)),
        route_config_provider_(std::move(route_config_provider)), factories_(std::move(factories)),
        drain_decision_(context.drainDecision()), tracer_(std::move(tracer)),
        tracing_config_(std::move(tracing_config)), access_logs_(std::move(access_logs)),
        time_source_(context.serverFactoryContext().timeSource()) {}

  // FilterConfig
  RouteEntryConstSharedPtr routeEntry(const MatchInput& request) const override {
    auto config = std::static_pointer_cast<const RouteMatcher>(route_config_provider_->config());
    return config->routeEntry(request);
  }
  const CodecFactory& codecFactory() const override { return *codec_factory_; }
  const Network::DrainDecision& drainDecision() const override { return drain_decision_; }
  OptRef<Tracing::Tracer> tracingProvider() const override {
    return makeOptRefFromPtr<Tracing::Tracer>(tracer_.get());
  }
  OptRef<const Tracing::ConnectionManagerTracingConfig> tracingConfig() const override {
    return makeOptRefFromPtr<const Tracing::ConnectionManagerTracingConfig>(tracing_config_.get());
  }
  GenericFilterStats& stats() override { return stats_; }
  const CodeOrFlags& codeOrFlags() const override { return code_or_flags_; }
  const std::vector<AccessLogInstanceSharedPtr>& accessLogs() const override {
    return access_logs_;
  }

  // FilterChainFactory
  void createFilterChain(FilterChainManager& manager) override {
    for (auto& factory : factories_) {
      manager.applyFilterFactoryCb({factory.config_name_}, factory.callback_);
    }
  }

private:
  friend class ActiveStream;
  friend class Filter;

  const std::string stat_prefix_;
  GenericFilterStats stats_;
  const CodeOrFlags& code_or_flags_;

  CodecFactoryPtr codec_factory_;

  Rds::RouteConfigProviderSharedPtr route_config_provider_;

  std::vector<NamedFilterFactoryCb> factories_;

  const Network::DrainDecision& drain_decision_;

  Tracing::TracerSharedPtr tracer_;
  Tracing::ConnectionManagerTracingConfigPtr tracing_config_;
  std::vector<AccessLogInstanceSharedPtr> access_logs_;

  TimeSource& time_source_;
};

class ActiveStream : public FilterChainManager,
                     public LinkedObject<ActiveStream>,
                     public Envoy::Event::DeferredDeletable,
                     public EncodingContext,
                     public Tracing::Config,
                     Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  class ActiveFilterBase : public virtual StreamFilterCallbacks {
  public:
    ActiveFilterBase(ActiveStream& parent, FilterContext context, bool is_dual)
        : parent_(parent), context_(context), is_dual_(is_dual) {}

    // StreamFilterCallbacks
    Envoy::Event::Dispatcher& dispatcher() override { return parent_.dispatcher(); }
    const CodecFactory& codecFactory() override { return parent_.codecFactory(); }
    const RouteEntry* routeEntry() const override { return parent_.routeEntry().ptr(); }
    const RouteSpecificFilterConfig* perFilterConfig() const override {
      if (const auto entry = parent_.routeEntry(); entry.has_value()) {
        return entry->perFilterConfig(context_.config_name);
      }
      return nullptr;
    }
    StreamInfo::StreamInfo& streamInfo() override { return parent_.stream_info_; }
    Tracing::Span& activeSpan() override { return parent_.activeSpan(); }
    OptRef<const Tracing::Config> tracingConfig() const override { return parent_.tracingConfig(); }
    const Network::Connection* connection() const override;
    absl::string_view filterConfigName() const override { return context_.config_name; }

    bool isDualFilter() const { return is_dual_; }

    ActiveStream& parent_;
    FilterContext context_;
    const bool is_dual_{};
  };

  class ActiveDecoderFilter : public ActiveFilterBase, public DecoderFilterCallback {
  public:
    ActiveDecoderFilter(ActiveStream& parent, FilterContext context, DecoderFilterSharedPtr filter,
                        bool is_dual)
        : ActiveFilterBase(parent, context, is_dual), filter_(std::move(filter)) {
      filter_->setDecoderFilterCallbacks(*this);
    }

    // DecoderFilterCallback
    void sendLocalReply(Status status, absl::string_view data,
                        ResponseUpdateFunction func) override {
      parent_.sendLocalReply(status, data, std::move(func));
    }
    void continueDecoding() override { parent_.continueDecoding(); }
    void onResponseHeaderFrame(ResponseHeaderFramePtr frame) override {
      parent_.onResponseHeaderFrame(std::move(frame));
    }
    void onResponseCommonFrame(ResponseCommonFramePtr frame) override {
      parent_.onResponseCommonFrame(std::move(frame));
    }
    void setRequestFramesHandler(RequestFramesHandler* handler) override {
      ASSERT(parent_.request_stream_frames_handler_ == nullptr,
             "request frames handler is already set");
      parent_.request_stream_frames_handler_ = handler;
    }
    void completeDirectly() override { parent_.completeStream(); }

    DecoderFilterSharedPtr filter_;
  };
  using ActiveDecoderFilterPtr = std::unique_ptr<ActiveDecoderFilter>;

  class ActiveEncoderFilter : public ActiveFilterBase, public EncoderFilterCallback {
  public:
    ActiveEncoderFilter(ActiveStream& parent, FilterContext context, EncoderFilterSharedPtr filter,
                        bool is_dual)
        : ActiveFilterBase(parent, context, is_dual), filter_(std::move(filter)) {
      filter_->setEncoderFilterCallbacks(*this);
    }

    // EncoderFilterCallback
    void continueEncoding() override { parent_.continueEncoding(); }

    EncoderFilterSharedPtr filter_;
  };
  using ActiveEncoderFilterPtr = std::unique_ptr<ActiveEncoderFilter>;

  class FilterChainFactoryCallbacksHelper : public FilterChainFactoryCallbacks {
  public:
    FilterChainFactoryCallbacksHelper(ActiveStream& parent, FilterContext context)
        : parent_(parent), context_(context) {}

    // FilterChainFactoryCallbacks
    void addDecoderFilter(DecoderFilterSharedPtr filter) override {
      parent_.addDecoderFilter(
          std::make_unique<ActiveDecoderFilter>(parent_, context_, std::move(filter), false));
    }
    void addEncoderFilter(EncoderFilterSharedPtr filter) override {
      parent_.addEncoderFilter(
          std::make_unique<ActiveEncoderFilter>(parent_, context_, std::move(filter), false));
    }
    void addFilter(StreamFilterSharedPtr filter) override {
      parent_.addDecoderFilter(
          std::make_unique<ActiveDecoderFilter>(parent_, context_, filter, true));
      parent_.addEncoderFilter(
          std::make_unique<ActiveEncoderFilter>(parent_, context_, std::move(filter), true));
    }

  private:
    ActiveStream& parent_;
    FilterContext context_;
  };

  ActiveStream(Filter& parent, RequestHeaderFramePtr request, absl::optional<StartTime> start_time);

  void addDecoderFilter(ActiveDecoderFilterPtr filter) {
    decoder_filters_.emplace_back(std::move(filter));
  }
  void addEncoderFilter(ActiveEncoderFilterPtr filter) {
    encoder_filters_.emplace_back(std::move(filter));
  }

  bool initializeFilterChain(FilterChainFactory& factory);

  Envoy::Event::Dispatcher& dispatcher();
  const CodecFactory& codecFactory();
  void resetStream(DownstreamStreamResetReason reason);
  StreamInfo::StreamInfoImpl& streamInfo() { return stream_info_; }

  void sendLocalReply(Status status, absl::string_view data, ResponseUpdateFunction func);
  void continueDecoding();
  void onRequestCommonFrame(RequestCommonFramePtr request_common_frame);

  void onResponseHeaderFrame(ResponseHeaderFramePtr response_header_frame);
  void onResponseCommonFrame(ResponseCommonFramePtr response_common_frame);
  void continueEncoding();

  // FilterChainManager
  void applyFilterFactoryCb(FilterContext context, FilterFactoryCb& factory) override {
    FilterChainFactoryCallbacksHelper callbacks(*this, context);
    factory(callbacks);
  }

  // EncodingContext
  OptRef<const RouteEntry> routeEntry() const override {
    return makeOptRefFromPtr<const RouteEntry>(cached_route_entry_.get());
  }

  Tracing::Span& activeSpan() {
    if (active_span_) {
      return *active_span_;
    } else {
      return Tracing::NullSpan::instance();
    }
  }

  OptRef<const Tracing::Config> tracingConfig() const {
    if (conn_manager_tracing_config_.has_value()) {
      return {*this};
    }
    return {};
  }

  void deferredDelete();
  void completeStream(absl::optional<DownstreamStreamResetReason> reason = {});

  uint64_t requestStreamId() const { return request_header_frame_->frameFlags().streamId(); }

  auto& decoderFiltersForTest() { return decoder_filters_; }
  auto& encoderFiltersForTest() { return encoder_filters_; }
  auto nextDecoderHeaderFilterForTest() { return decoder_filter_iter_header_; }
  auto nextDecoderCommonFilterForTest() { return decoder_filter_iter_common_; }
  auto nextEncoderHeaderFilterForTest() { return encoder_filter_iter_header_; }
  auto nextEncoderCommonFilterForTest() { return encoder_filter_iter_common_; }

private:
  // Keep these methods private to ensure that these methods are only called by the reference
  // returned by the public tracingConfig() method.
  // Tracing::TracingConfig
  Tracing::OperationName operationName() const override;
  const Tracing::CustomTagMap* customTags() const override;
  bool verbose() const override;
  uint32_t maxPathTagLength() const override;
  bool spawnUpstreamSpan() const override;

  void sendRequestFrameToUpstream();

  bool sendFrameToDownstream(const StreamFrame& frame, bool header_frame);

  void processRequestHeaderFrame();
  void processRequestCommonFrame();
  void processResponseHeaderFrame();
  void processResponseCommonFrame();

  bool stream_reset_or_complete_{false};
  bool waiting_request_frames_{false};

  Filter& parent_;

  RequestHeaderFramePtr request_header_frame_;
  std::list<RequestCommonFramePtr> request_common_frames_;

  // The request common frame that is being processed.
  RequestCommonFramePtr request_common_frame_;
  RequestFramesHandler* request_stream_frames_handler_{nullptr};

  ResponseHeaderFramePtr response_header_frame_;
  std::list<ResponseCommonFramePtr> response_common_frames_;
  bool local_reply_{false};

  // The response common frame that is being processed.
  ResponseCommonFramePtr response_common_frame_;

  RouteEntryConstSharedPtr cached_route_entry_;

  using DecoderFilters = absl::InlinedVector<ActiveDecoderFilterPtr, 8>;
  using EncoderFilters = absl::InlinedVector<ActiveEncoderFilterPtr, 8>;

  DecoderFilters decoder_filters_;
  DecoderFilters::iterator decoder_filter_iter_header_{};
  DecoderFilters::iterator decoder_filter_iter_common_{};
  bool stop_decoder_filter_chain_{};

  EncoderFilters encoder_filters_;
  EncoderFilters::iterator encoder_filter_iter_header_{};
  EncoderFilters::iterator encoder_filter_iter_common_{};
  bool stop_encoder_filter_chain_{};

  StreamInfo::StreamInfoImpl stream_info_;

  OptRef<const Tracing::ConnectionManagerTracingConfig> conn_manager_tracing_config_;
  Tracing::SpanPtr active_span_;
};
using ActiveStreamPtr = std::unique_ptr<ActiveStream>;

class Filter : public Envoy::Network::ReadFilter,
               public Network::ConnectionCallbacks,
               public Envoy::Logger::Loggable<Envoy::Logger::Id::filter>,
               public ServerCodecCallbacks {
public:
  Filter(FilterConfigSharedPtr config, Server::Configuration::FactoryContext& context)
      : config_(std::move(config)),
        stats_helper_(config_->codeOrFlags(), config_->stats(), context.scope()),
        drain_decision_(config_->drainDecision()),
        time_source_(context.serverFactoryContext().timeSource()),
        runtime_(context.serverFactoryContext().runtime()),
        cluster_manager_(context.serverFactoryContext().clusterManager()) {
    server_codec_ = config_->codecFactory().createServerCodec();
    server_codec_->setCodecCallbacks(*this);
  }

  // Envoy::Network::ReadFilter
  Envoy::Network::FilterStatus onData(Envoy::Buffer::Instance& data, bool end_stream) override;
  Envoy::Network::FilterStatus onNewConnection() override {
    return Envoy::Network::FilterStatus::Continue;
  }
  void initializeReadFilterCallbacks(Envoy::Network::ReadFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
    callbacks_->connection().addConnectionCallbacks(*this);
  }

  // ServerCodecCallbacks
  void onDecodingSuccess(RequestHeaderFramePtr header_frame,
                         absl::optional<StartTime> start_time = {}) override;
  void onDecodingSuccess(RequestCommonFramePtr common_frame) override;
  void onDecodingFailure(absl::string_view reason = {}) override;
  void writeToConnection(Buffer::Instance& buffer) override;
  OptRef<Network::Connection> connection() override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override {
    ENVOY_LOG(debug, "generic proxy: downstream connection event {}", static_cast<uint32_t>(event));
    if (event == Network::ConnectionEvent::Connected ||
        event == Network::ConnectionEvent::ConnectedZeroRtt) {
      return;
    }
    downstream_connection_closed_ = true;
    resetDownstreamAllStreams(event == Network::ConnectionEvent::LocalClose
                                  ? DownstreamStreamResetReason::LocalConnectionTermination
                                  : DownstreamStreamResetReason::ConnectionTermination);
  }
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  Network::Connection& downstreamConnection() {
    ASSERT(callbacks_ != nullptr);
    return callbacks_->connection();
  }

  /**
   * Create a new active stream and add it to the active stream list.
   * @param request the request to be processed.
   * @param start_time the start time of the request.
   */
  void newDownstreamRequest(StreamRequestPtr request, absl::optional<StartTime> start_time = {});

  static const std::string& name() {
    CONSTRUCT_ON_FIRST_USE(std::string, "envoy.filters.network.generic_proxy");
  }

  std::list<ActiveStreamPtr>& activeStreamsForTest() { return active_streams_; }
  const auto& frameHandlersForTest() { return frame_handlers_; }

  // This may be called multiple times in some scenarios. But it is safe.
  void resetDownstreamAllStreams(DownstreamStreamResetReason reason);
  // This may be called multiple times in some scenarios. But it is safe.
  void closeDownstreamConnection();

  void mayBeDrainClose();

protected:
  // This will be called when drain decision is made and all active streams are handled.
  // This is a virtual method so that it can be overridden by derived classes.
  virtual void onDrainCloseAndNoActiveStreams();

  Envoy::Network::ReadFilterCallbacks* callbacks_{nullptr};

private:
  friend class ActiveStream;
  friend class UpstreamManagerImpl;

  void registerFrameHandler(uint64_t stream_id, ActiveStream* stream);
  void unregisterFrameHandler(uint64_t stream_id);

  bool downstream_connection_closed_{};

  FilterConfigSharedPtr config_{};
  GenericFilterStatsHelper stats_helper_;

  const Network::DrainDecision& drain_decision_;
  bool stream_drain_decision_{};
  TimeSource& time_source_;
  Runtime::Loader& runtime_;
  Upstream::ClusterManager& cluster_manager_;

  ServerCodecPtr server_codec_;

  Buffer::OwnedImpl response_buffer_;

  std::list<ActiveStreamPtr> active_streams_;
  absl::flat_hash_map<uint64_t, ActiveStream*> frame_handlers_;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
