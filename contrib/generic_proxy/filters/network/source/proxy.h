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
#include "contrib/generic_proxy/filters/network/source/upstream.h"
#include "stats.h"

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
                   Envoy::Server::Configuration::FactoryContext& context)
      : stat_prefix_(stat_prefix),
        stats_(GenericFilterStats::generateStats(stat_prefix_, context.scope())),
        codec_factory_(std::move(codec)), route_config_provider_(std::move(route_config_provider)),
        factories_(std::move(factories)), drain_decision_(context.drainDecision()),
        tracer_(std::move(tracer)), tracing_config_(std::move(tracing_config)),
        time_source_(context.timeSource()) {}

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

  TimeSource& time_source_;
};

class ActiveStream : public FilterChainManager,
                     public LinkedObject<ActiveStream>,
                     public Envoy::Event::DeferredDeletable,
                     public ResponseEncoderCallback,
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
    absl::optional<ExtendedOptions> requestOptions() const override {
      return parent_.downstream_request_options_;
    }
    absl::optional<ExtendedOptions> responseOptions() const override {
      return parent_.local_or_upstream_response_options_;
    }
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
    void upstreamResponse(ResponsePtr response, ExtendedOptions options) override {
      parent_.upstreamResponse(std::move(response), options);
    }
    void completeDirectly() override { parent_.completeDirectly(); }
    void bindUpstreamConn(Upstream::TcpPoolData&& pool_data) override;
    OptRef<UpstreamManager> boundUpstreamConn() override;

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

  ActiveStream(Filter& parent, RequestPtr request, ExtendedOptions request_options);

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
  void upstreamResponse(ResponsePtr response, ExtendedOptions options);
  void completeDirectly();

  void continueEncoding();

  // FilterChainManager
  void applyFilterFactoryCb(FilterContext context, FilterFactoryCb& factory) override {
    FilterChainFactoryCallbacksHelper callbacks(*this, context);
    factory(callbacks);
  }

  // ResponseEncoderCallback
  void onEncodingSuccess(Buffer::Instance& buffer) override;

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

private:
  // Keep these methods private to ensure that these methods are only called by the reference
  // returned by the public tracingConfig() method.
  // Tracing::TracingConfig
  Tracing::OperationName operationName() const override;
  const Tracing::CustomTagMap* customTags() const override;
  bool verbose() const override;
  uint32_t maxPathTagLength() const override;

  bool active_stream_reset_{false};

  Filter& parent_;

  RequestPtr downstream_request_stream_;
  absl::optional<ExtendedOptions> downstream_request_options_;

  ResponsePtr local_or_upstream_response_stream_;
  absl::optional<ExtendedOptions> local_or_upstream_response_options_;

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

class UpstreamManagerImpl : public UpstreamConnection,
                            public UpstreamManager,
                            public ResponseDecoderCallback {
public:
  UpstreamManagerImpl(Filter& parent, Upstream::TcpPoolData&& tcp_pool_data);

  // UpstreamConnection
  void onPoolSuccessImpl() override;
  void onPoolFailureImpl(ConnectionPool::PoolFailureReason reason,
                         absl::string_view transport_failure_reason) override;
  void onEventImpl(Network::ConnectionEvent event) override;

  // ResponseDecoderCallback
  void onDecodingSuccess(ResponsePtr response, ExtendedOptions options) override;
  void onDecodingFailure() override;
  void writeToConnection(Buffer::Instance& buffer) override;
  OptRef<Network::Connection> connection() override;

  // UpstreamManager
  void registerResponseCallback(uint64_t stream_id, PendingResponseCallback& cb) override;
  void registerUpstreamCallback(uint64_t stream_id, UpstreamBindingCallback& cb) override;
  void unregisterResponseCallback(uint64_t stream_id) override;
  void unregisterUpstreamCallback(uint64_t stream_id) override;

  Filter& parent_;

  absl::flat_hash_map<uint64_t, PendingResponseCallback*> registered_response_callbacks_;
  absl::flat_hash_map<uint64_t, UpstreamBindingCallback*> registered_upstream_callbacks_;
};

class Filter : public Envoy::Network::ReadFilter,
               public Network::ConnectionCallbacks,
               public Envoy::Logger::Loggable<Envoy::Logger::Id::filter>,
               public RequestDecoderCallback {
public:
  Filter(FilterConfigSharedPtr config, TimeSource& time_source, Runtime::Loader& runtime)
      : config_(std::move(config)), stats_(config_->stats()),
        drain_decision_(config_->drainDecision()), time_source_(time_source), runtime_(runtime) {
    request_decoder_ = config_->codecFactory().requestDecoder();
    request_decoder_->setDecoderCallback(*this);
    response_encoder_ = config_->codecFactory().responseEncoder();
    message_creator_ = config_->codecFactory().messageCreator();
    protocol_options_ = config_->codecFactory().protocolOptions();
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
  void onDecodingSuccess(RequestPtr request, ExtendedOptions options) override;
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

    // If these is bound upstream connection, clean it up.
    if (upstream_manager_ != nullptr) {
      upstream_manager_->cleanUp(true);
      downstreamConnection().dispatcher().deferredDelete(std::move(upstream_manager_));
      upstream_manager_ = nullptr;
    }
  }
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  void sendReplyDownstream(Response& response, ResponseEncoderCallback& callback);

  Network::Connection& downstreamConnection() {
    ASSERT(callbacks_ != nullptr);
    return callbacks_->connection();
  }

  /**
   * Create a new active stream and add it to the active stream list.
   * @param request the request to be processed.
   * @param options the extended options for the request.
   */
  void newDownstreamRequest(RequestPtr request, ExtendedOptions options);

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

  // This may be called multiple times in some scenarios. But it is safe.
  void resetStreamsForUnexpectedError();
  // This may be called multiple times in some scenarios. But it is safe.
  void closeDownstreamConnection();

  void mayBeDrainClose();

  void bindUpstreamConn(Upstream::TcpPoolData&& tcp_pool_data);
  OptRef<UpstreamManager> boundUpstreamConn() {
    return makeOptRefFromPtr<UpstreamManager>(upstream_manager_.get());
  }

  void onBoundUpstreamConnectionEvent(Network::ConnectionEvent event);

protected:
  // This will be called when drain decision is made and all active streams are handled.
  // This is a virtual method so that it can be overridden by derived classes.
  virtual void onDrainCloseAndNoActiveStreams();

  Envoy::Network::ReadFilterCallbacks* callbacks_{nullptr};

private:
  friend class ActiveStream;
  friend class UpstreamManagerImpl;

  bool downstream_connection_closed_{};

  FilterConfigSharedPtr config_{};
  GenericFilterStats& stats_;
  const Network::DrainDecision& drain_decision_;
  bool stream_drain_decision_{};
  TimeSource& time_source_;
  Runtime::Loader& runtime_;

  RequestDecoderPtr request_decoder_;
  ResponseEncoderPtr response_encoder_;
  MessageCreatorPtr message_creator_;
  ProtocolOptions protocol_options_;

  Buffer::OwnedImpl response_buffer_;

  // The upstream connection manager.
  std::unique_ptr<UpstreamManagerImpl> upstream_manager_;

  std::list<ActiveStreamPtr> active_streams_;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
