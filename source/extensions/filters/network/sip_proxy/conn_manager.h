#pragma once

#include "envoy/common/pure.h"
#include "envoy/common/random_generator.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/timespan.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/linked_object.h"
#include "common/common/logger.h"
#include "common/stats/timespan_impl.h"
#include "common/stream_info/stream_info_impl.h"

#include "extensions/filters/network/sip_proxy/decoder.h"
#include "extensions/filters/network/sip_proxy/filters/filter.h"
#include "extensions/filters/network/sip_proxy/protocol.h"
#include "extensions/filters/network/sip_proxy/stats.h"
#include "extensions/filters/network/sip_proxy/transport.h"

#include "absl/types/any.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

/**
 * Config is a configuration interface for ConnectionManager.
 */
class SipSettings;
class Config {
public:
  virtual ~Config() = default;

  virtual SipFilters::FilterChainFactory& filterFactory() PURE;
  virtual SipFilterStats& stats() PURE;
  virtual Router::Config& routerConfig() PURE;
  virtual std::shared_ptr<SipSettings> settings() PURE;
};

/**
 * Extends Upstream::ProtocolOptionsConfig with Sip-specific cluster options.
 */
class ProtocolOptionsConfig : public Upstream::ProtocolOptionsConfig {
public:
  ~ProtocolOptionsConfig() override = default;
};

/**
 * ConnectionManager is a Network::Filter that will perform Sip request handling on a connection.
 */
class ConnectionManager : public Network::ReadFilter,
                          public Network::ConnectionCallbacks,
                          public DecoderCallbacks,
                          Logger::Loggable<Logger::Id::filter> {
public:
  ConnectionManager(Config& config, Random::RandomGenerator& random_generator,
                    TimeSource& time_system,
                    std::shared_ptr<Router::TransactionInfos> transaction_infos);
  ~ConnectionManager() override;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks&) override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  // DecoderCallbacks
  DecoderEventHandler& newDecoderEventHandler(MessageMetadataSharedPtr metadata) override;

private:
  struct ActiveTrans;

  struct ResponseDecoder : public DecoderCallbacks, public DecoderEventHandler {
    ResponseDecoder(ActiveTrans& parent)
        : parent_(parent), decoder_(std::make_unique<Decoder>(*this)), complete_(false),
          first_reply_field_(false) {}

    bool onData(MessageMetadataSharedPtr metadata);

    // DecoderEventHandler
    FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;
    FilterStatus messageEnd() override;
    FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override {
      UNREFERENCED_PARAMETER(metadata);
      return FilterStatus::Continue;
    }
    FilterStatus transportEnd() override;

    // DecoderCallbacks
    DecoderEventHandler& newDecoderEventHandler(MessageMetadataSharedPtr metadata) override {
      UNREFERENCED_PARAMETER(metadata);
      return *this;
    }

    ActiveTrans& parent_;
    DecoderPtr decoder_;
    Buffer::OwnedImpl upstream_buffer_;
    MessageMetadataSharedPtr metadata_;
    absl::optional<bool> success_;
    bool complete_ : 1;
    bool first_reply_field_ : 1;
  };
  using ResponseDecoderPtr = std::unique_ptr<ResponseDecoder>;

  // Wraps a DecoderFilter and acts as the DecoderFilterCallbacks for the filter, enabling filter
  // chain continuation.
  struct ActiveTransDecoderFilter : public SipFilters::DecoderFilterCallbacks,
                                    LinkedObject<ActiveTransDecoderFilter> {
    ActiveTransDecoderFilter(ActiveTrans& parent, SipFilters::DecoderFilterSharedPtr filter)
        : parent_(parent), handle_(filter) {}

    // SipFilters::DecoderFilterCallbacks
    uint64_t streamId() const override { return parent_.streamId(); }
    std::string transactionId() const override { return parent_.transactionId(); }
    const Network::Connection* connection() const override { return parent_.connection(); }
    void continueDecoding() override;
    Router::RouteConstSharedPtr route() override { return parent_.route(); }
    void sendLocalReply(const DirectResponse& response, bool end_stream) override {
      parent_.sendLocalReply(response, end_stream);
    }
    void startUpstreamResponse() override { parent_.startUpstreamResponse(); }
    SipFilters::ResponseStatus upstreamData(MessageMetadataSharedPtr metadata) override {
      return parent_.upstreamData(metadata);
    }
    void resetDownstreamConnection() override { parent_.resetDownstreamConnection(); }
    StreamInfo::StreamInfo& streamInfo() override { return parent_.streamInfo(); }
    std::shared_ptr<Router::TransactionInfos> transactionInfos() override {
      return parent_.transactionInfos();
    }
    std::shared_ptr<SipSettings> settings() override { return parent_.settings(); }
    void onReset() override { return parent_.onReset(); }

    ActiveTrans& parent_;
    SipFilters::DecoderFilterSharedPtr handle_;
  };
  using ActiveTransDecoderFilterPtr = std::unique_ptr<ActiveTransDecoderFilter>;

  // ActiveTrans tracks request/response pairs.
  struct ActiveTrans : LinkedObject<ActiveTrans>,
                       public Event::DeferredDeletable,
                       public DecoderEventHandler,
                       public SipFilters::DecoderFilterCallbacks,
                       public SipFilters::FilterChainFactoryCallbacks {
    ActiveTrans(ConnectionManager& parent, MessageMetadataSharedPtr metadata)
        : parent_(parent), request_timer_(new Stats::HistogramCompletableTimespanImpl(
                               parent_.stats_.request_time_ms_, parent_.time_source_)),
          stream_id_(parent_.random_generator_.random()),
          transaction_id_(metadata->transactionId().value()),
          stream_info_(parent_.time_source_, parent_.read_callbacks_->connection().addressProviderSharedPtr()), metadata_(metadata), local_response_sent_{false},
          pending_transport_end_{false}, timestamp_{std::chrono::system_clock::now()} {
      parent_.stats_.request_active_.inc();
    }
    ~ActiveTrans() override {
      // request_timer_->complete();
      ENVOY_LOG(trace, "destruct activetrans {}", transaction_id_);
      parent_.stats_.request_active_.dec();

      for (auto& filter : decoder_filters_) {
        filter->handle_->onDestroy();
      }
    }

    // DecoderEventHandler
    FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override;
    FilterStatus transportEnd() override;
    FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;
    FilterStatus messageEnd() override;

    // SipFilters::DecoderFilterCallbacks
    uint64_t streamId() const override { return stream_id_; }
    std::string transactionId() const override { return transaction_id_; }
    const Network::Connection* connection() const override;
    void continueDecoding() override { return parent_.continueDecoding(); }
    Router::RouteConstSharedPtr route() override;
    void sendLocalReply(const DirectResponse& response, bool end_stream) override;
    void startUpstreamResponse() override;
    SipFilters::ResponseStatus upstreamData(MessageMetadataSharedPtr metadata) override;
    void resetDownstreamConnection() override;
    StreamInfo::StreamInfo& streamInfo() override { return stream_info_; }

    std::shared_ptr<Router::TransactionInfos> transactionInfos() override {
      return parent_.transaction_infos_;
    }
    std::shared_ptr<SipSettings> settings() override { return parent_.config_.settings(); }
    void onReset() override;

    // Sip::FilterChainFactoryCallbacks
    void addDecoderFilter(SipFilters::DecoderFilterSharedPtr filter) override {
      ActiveTransDecoderFilterPtr wrapper =
          std::make_unique<ActiveTransDecoderFilter>(*this, filter);
      filter->setDecoderFilterCallbacks(*wrapper);
      LinkedList::moveIntoListBack(std::move(wrapper), decoder_filters_);
    }

    FilterStatus applyDecoderFilters(ActiveTransDecoderFilter* filter);
    void finalizeRequest();

    void createFilterChain();
    void onError(const std::string& what);

    ConnectionManager& parent_;
    Stats::TimespanPtr request_timer_;
    uint64_t stream_id_;
    std::string transaction_id_;
    StreamInfo::StreamInfoImpl stream_info_;
    MessageMetadataSharedPtr metadata_;
    std::list<ActiveTransDecoderFilterPtr> decoder_filters_;
    ResponseDecoderPtr response_decoder_;
    absl::optional<Router::RouteConstSharedPtr> cached_route_;
    Buffer::OwnedImpl response_buffer_;
    int32_t original_sequence_id_{0};
    std::function<FilterStatus(DecoderEventHandler*)> filter_action_;
    absl::any filter_context_;
    bool local_response_sent_ : 1;
    bool pending_transport_end_ : 1;

    /* Used by Router */
    std::shared_ptr<Router::TransactionInfos> transaction_infos_;
    std::chrono::system_clock::time_point timestamp_;
  };

  using ActiveTransPtr = std::unique_ptr<ActiveTrans>;

  void continueDecoding();
  void dispatch();
  void sendLocalReply(MessageMetadata& metadata, const DirectResponse& response, bool end_stream);
  void doDeferredRpcDestroy(ActiveTrans& rpc);
  void resetAllRpcs(bool local_reset);

  Config& config_;
  SipFilterStats& stats_;

  Network::ReadFilterCallbacks* read_callbacks_{};

  DecoderPtr decoder_;
  std::unordered_map<std::string, ActiveTransPtr> transactions_;
  Buffer::OwnedImpl request_buffer_;
  Random::RandomGenerator& random_generator_;
  TimeSource& time_source_;

  /* This is used in Router, put here to pass to Router */
  std::shared_ptr<Router::TransactionInfos> transaction_infos_;
  std::shared_ptr<SipSettings> sip_settings_;
};

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
