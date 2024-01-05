#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/core/v3/extension.pb.h"
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

#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.validate.h"
#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/route.pb.h"
#include "contrib/generic_proxy/filters/network/source/interface/filter.h"
#include "contrib/generic_proxy/filters/network/source/interface/proxy_config.h"
#include "contrib/generic_proxy/filters/network/source/interface/route.h"
#include "contrib/generic_proxy/filters/network/source/interface/stream.h"
#include "contrib/generic_proxy/filters/network/source/rds.h"
#include "contrib/generic_proxy/filters/network/source/rds_impl.h"
#include "contrib/generic_proxy/filters/network/source/route.h"
#include "contrib/generic_proxy/filters/network/source/stats.h"
#include "contrib/generic_proxy/filters/network/source/tracing.h"

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

class FilterConfigImpl : public FilterConfig {
public:
  FilterConfigImpl(const std::string& stat_prefix, CodecFactoryPtr codec,
                   Rds::RouteConfigProviderSharedPtr route_config_provider,
                   std::vector<NamedFilterFactoryCb> factories, Tracing::TracerSharedPtr tracer,
                   Tracing::ConnectionManagerTracingConfigPtr tracing_config,
                   std::vector<AccessLogInstanceSharedPtr>&& access_logs,
                   Envoy::Server::Configuration::FactoryContext& context)
      : stat_prefix_(stat_prefix),
        stats_(GenericFilterStats::generateStats(stat_prefix_, context.scope())),
        codec_factory_(std::move(codec)), route_config_provider_(std::move(route_config_provider)),
        factories_(std::move(factories)), drain_decision_(context.drainDecision()),
        tracer_(std::move(tracer)), tracing_config_(std::move(tracing_config)),
        access_logs_(std::move(access_logs)),
        time_source_(context.serverFactoryContext().timeSource()) {}

  // FilterConfig
  RouteEntryConstSharedPtr routeEntry(const Request& request) const override {
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
                     public EncodingCallbacks,
                     public Tracing::Config,
                     Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  class ActiveFilterBase : public virtual StreamFilterCallbacks {
  public:
    ActiveFilterBase(ActiveStream& parent, FilterContext context, bool is_dual)
        : parent_(parent), context_(context), is_dual_(is_dual) {}

    // StreamFilterCallbacks
    Envoy::Event::Dispatcher& dispatcher() override { return parent_.dispatcher(); }
    const CodecFactory& downstreamCodec() override { return parent_.downstreamCodec(); }
    void resetStream() override { parent_.resetStream(); }
    const RouteEntry* routeEntry() const override { return parent_.routeEntry(); }
    const RouteSpecificFilterConfig* perFilterConfig() const override {
      if (const auto* entry = parent_.routeEntry(); entry != nullptr) {
        return entry->perFilterConfig(context_.config_name);
      }
      return nullptr;
    }
    const StreamInfo::StreamInfo& streamInfo() const override { return parent_.stream_info_; }
    StreamInfo::StreamInfo& streamInfo() override { return parent_.stream_info_; }
    Tracing::Span& activeSpan() override { return parent_.activeSpan(); }
    OptRef<const Tracing::Config> tracingConfig() const override { return parent_.tracingConfig(); }
    const Network::Connection* connection() const override;

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
    void sendLocalReply(Status status, ResponseUpdateFunction&& func) override {
      parent_.sendLocalReply(status, std::move(func));
    }
    void continueDecoding() override { parent_.continueDecoding(); }
    void onResponseStart(ResponsePtr response) override {
      parent_.onResponseStart(std::move(response));
    }
    void onResponseFrame(StreamFramePtr frame) override {
      parent_.onResponseFrame(std::move(frame));
    }
    void setRequestFramesHandler(StreamFrameHandler& handler) override {
      ASSERT(parent_.request_stream_frame_handler_ == nullptr,
             "request frames handler is already set");
      parent_.request_stream_frame_handler_ = &handler;
    }
    void completeDirectly() override { parent_.completeDirectly(); }

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

  ActiveStream(Filter& parent, StreamRequestPtr request);

  void addDecoderFilter(ActiveDecoderFilterPtr filter) {
    decoder_filters_.emplace_back(std::move(filter));
  }
  void addEncoderFilter(ActiveEncoderFilterPtr filter) {
    encoder_filters_.emplace_back(std::move(filter));
  }

  void initializeFilterChain(FilterChainFactory& factory);

  Envoy::Event::Dispatcher& dispatcher();
  const CodecFactory& downstreamCodec();
  void resetStream();
  const RouteEntry* routeEntry() const { return cached_route_entry_.get(); }

  void sendLocalReply(Status status, ResponseUpdateFunction&&);
  void continueDecoding();
  void onResponseStart(StreamResponsePtr response);
  void onResponseFrame(StreamFramePtr frame);
  void completeDirectly();

  void continueEncoding();

  // FilterChainManager
  void applyFilterFactoryCb(FilterContext context, FilterFactoryCb& factory) override {
    FilterChainFactoryCallbacksHelper callbacks(*this, context);
    factory(callbacks);
  }

  // ResponseEncoderCallback
  void onEncodingSuccess(Buffer::Instance& buffer, bool end_stream) override;

  void onRequestFrame(StreamFramePtr frame);

  std::vector<ActiveDecoderFilterPtr>& decoderFiltersForTest() { return decoder_filters_; }
  std::vector<ActiveEncoderFilterPtr>& encoderFiltersForTest() { return encoder_filters_; }
  size_t nextDecoderFilterIndexForTest() { return next_decoder_filter_index_; }
  size_t nextEncoderFilterIndexForTest() { return next_encoder_filter_index_; }

  Tracing::Span& activeSpan() {
    if (active_span_) {
      return *active_span_;
    } else {
      return Tracing::NullSpan::instance();
    }
  }

  OptRef<const Tracing::Config> tracingConfig() const {
    if (connection_manager_tracing_config_.has_value()) {
      return {*this};
    }
    return {};
  }

  void completeRequest();

  uint64_t requestStreamId() const {
    return request_stream_->frameFlags().streamFlags().streamId();
  }

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

  void sendResponseStartToDownstream();
  void sendResponseFrameToDownstream();

  bool active_stream_reset_{false};

  bool registered_in_frame_handlers_{false};

  Filter& parent_;

  StreamRequestPtr request_stream_;
  std::list<StreamFramePtr> request_stream_frames_;
  bool request_stream_end_{false};
  bool request_filter_chain_complete_{false};

  StreamFrameHandler* request_stream_frame_handler_{nullptr};

  StreamResponsePtr response_stream_;
  std::list<StreamFramePtr> response_stream_frames_;
  bool response_stream_end_{false};
  bool response_filter_chain_complete_{false};

  RouteEntryConstSharedPtr cached_route_entry_;

  std::vector<ActiveDecoderFilterPtr> decoder_filters_;
  size_t next_decoder_filter_index_{0};

  std::vector<ActiveEncoderFilterPtr> encoder_filters_;
  size_t next_encoder_filter_index_{0};

  StreamInfo::StreamInfoImpl stream_info_;

  Stats::TimespanPtr request_timer_;

  OptRef<const Tracing::ConnectionManagerTracingConfig> connection_manager_tracing_config_;
  Tracing::SpanPtr active_span_;
};
using ActiveStreamPtr = std::unique_ptr<ActiveStream>;

class Filter : public Envoy::Network::ReadFilter,
               public Network::ConnectionCallbacks,
               public Envoy::Logger::Loggable<Envoy::Logger::Id::filter>,
               public ServerCodecCallbacks {
public:
  Filter(FilterConfigSharedPtr config, TimeSource& time_source, Runtime::Loader& runtime)
      : config_(std::move(config)), stats_(config_->stats()),
        drain_decision_(config_->drainDecision()), time_source_(time_source), runtime_(runtime) {
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

  // RequestDecoderCallback
  void onDecodingSuccess(StreamFramePtr request) override;
  void onDecodingFailure() override;
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
    resetStreamsForUnexpectedError();
  }
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  void sendFrameToDownstream(StreamFrame& frame, EncodingCallbacks& callbacks);

  Network::Connection& downstreamConnection() {
    ASSERT(callbacks_ != nullptr);
    return callbacks_->connection();
  }

  /**
   * Create a new active stream and add it to the active stream list.
   * @param request the request to be processed.
   */
  void newDownstreamRequest(StreamRequestPtr request);

  /**
   * Move the stream to the deferred delete stream list. This is called when the stream is reset
   * or completed.
   * @param stream the stream to be deferred deleted.
   */
  void deferredStream(ActiveStream& stream);

  static const std::string& name() {
    CONSTRUCT_ON_FIRST_USE(std::string, "envoy.filters.network.generic_proxy");
  }

  std::list<ActiveStreamPtr>& activeStreamsForTest() { return active_streams_; }
  const auto& frameHandlersForTest() { return frame_handlers_; }

  // This may be called multiple times in some scenarios. But it is safe.
  void resetStreamsForUnexpectedError();
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
  GenericFilterStats& stats_;
  const Network::DrainDecision& drain_decision_;
  bool stream_drain_decision_{};
  TimeSource& time_source_;
  Runtime::Loader& runtime_;

  ServerCodecPtr server_codec_;

  Buffer::OwnedImpl response_buffer_;

  std::list<ActiveStreamPtr> active_streams_;
  absl::flat_hash_map<uint64_t, ActiveStream*> frame_handlers_;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
