#pragma once

#include "envoy/common/pure.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/timespan.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/linked_object.h"
#include "common/common/logger.h"

#include "extensions/filters/network/thrift_proxy/decoder.h"
#include "extensions/filters/network/thrift_proxy/filters/filter.h"
#include "extensions/filters/network/thrift_proxy/protocol.h"
#include "extensions/filters/network/thrift_proxy/protocol_converter.h"
#include "extensions/filters/network/thrift_proxy/stats.h"
#include "extensions/filters/network/thrift_proxy/transport.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * Config is a configuration interface for ConnectionManager.
 */
class Config {
public:
  virtual ~Config() {}

  virtual ThriftFilters::FilterChainFactory& filterFactory() PURE;
  virtual ThriftFilterStats& stats() PURE;
  virtual TransportPtr createTransport() PURE;
  virtual ProtocolPtr createProtocol() PURE;
  virtual Router::Config& routerConfig() PURE;
};

/**
 * Extends Upstream::ProtocolOptionsConfig with Thrift-specific cluster options.
 */
class ProtocolOptionsConfig : public Upstream::ProtocolOptionsConfig {
public:
  virtual ~ProtocolOptionsConfig() {}

  virtual TransportType transport(TransportType downstream_transport) const PURE;
  virtual ProtocolType protocol(ProtocolType downstream_protocol) const PURE;
};

/**
 * ConnectionManager is a Network::Filter that will perform Thrift request handling on a connection.
 */
class ConnectionManager : public Network::ReadFilter,
                          public Network::ConnectionCallbacks,
                          public DecoderCallbacks,
                          Logger::Loggable<Logger::Id::thrift> {
public:
  ConnectionManager(Config& config, Runtime::RandomGenerator& random_generator);
  ~ConnectionManager();

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks&) override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  // DecoderCallbacks
  DecoderEventHandler& newDecoderEventHandler() override;

private:
  struct ActiveRpc;

  struct ResponseDecoder : public DecoderCallbacks, public ProtocolConverter {
    ResponseDecoder(ActiveRpc& parent, Transport& transport, Protocol& protocol)
        : parent_(parent), decoder_(std::make_unique<Decoder>(transport, protocol, *this)),
          complete_(false), first_reply_field_(false) {
      initProtocolConverter(*parent_.parent_.protocol_, parent_.response_buffer_);
    }

    bool onData(Buffer::Instance& data);

    // ProtocolConverter
    FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;
    FilterStatus fieldBegin(absl::string_view name, FieldType field_type,
                            int16_t field_id) override;
    FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override {
      UNREFERENCED_PARAMETER(metadata);
      return FilterStatus::Continue;
    }
    FilterStatus transportEnd() override;

    // DecoderCallbacks
    DecoderEventHandler& newDecoderEventHandler() override { return *this; }

    ActiveRpc& parent_;
    DecoderPtr decoder_;
    Buffer::OwnedImpl upstream_buffer_;
    MessageMetadataSharedPtr metadata_;
    absl::optional<bool> success_;
    bool complete_ : 1;
    bool first_reply_field_ : 1;
  };
  typedef std::unique_ptr<ResponseDecoder> ResponseDecoderPtr;

  // ActiveRpc tracks request/response pairs.
  struct ActiveRpc : LinkedObject<ActiveRpc>,
                     public Event::DeferredDeletable,
                     public DelegatingDecoderEventHandler,
                     public ThriftFilters::DecoderFilterCallbacks,
                     public ThriftFilters::FilterChainFactoryCallbacks {
    ActiveRpc(ConnectionManager& parent)
        : parent_(parent), request_timer_(new Stats::Timespan(parent_.stats_.request_time_ms_)),
          stream_id_(parent_.random_generator_.random()) {
      parent_.stats_.request_active_.inc();
    }
    ~ActiveRpc() {
      request_timer_->complete();
      parent_.stats_.request_active_.dec();

      if (decoder_filter_ != nullptr) {
        decoder_filter_->onDestroy();
      }
    }

    // DecoderEventHandler
    FilterStatus transportEnd() override;
    FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;

    // ThriftFilters::DecoderFilterCallbacks
    uint64_t streamId() const override { return stream_id_; }
    const Network::Connection* connection() const override;
    void continueDecoding() override;
    Router::RouteConstSharedPtr route() override;
    TransportType downstreamTransportType() const override {
      return parent_.decoder_->transportType();
    }
    ProtocolType downstreamProtocolType() const override {
      return parent_.decoder_->protocolType();
    }
    void sendLocalReply(const DirectResponse& response) override;
    void startUpstreamResponse(Transport& transport, Protocol& protocol) override;
    bool upstreamData(Buffer::Instance& buffer) override;
    void resetDownstreamConnection() override;

    // Thrift::FilterChainFactoryCallbacks
    void addDecoderFilter(ThriftFilters::DecoderFilterSharedPtr filter) override {
      // TODO(zuercher): support multiple filters
      filter->setDecoderFilterCallbacks(*this);
      decoder_filter_ = filter;
      event_handler_ = decoder_filter_.get();
    }

    void createFilterChain();
    void onReset();
    void onError(const std::string& what);

    ConnectionManager& parent_;
    Stats::TimespanPtr request_timer_;
    uint64_t stream_id_;
    MessageMetadataSharedPtr metadata_;
    ThriftFilters::DecoderFilterSharedPtr decoder_filter_;
    DecoderEventHandlerSharedPtr upgrade_handler_;
    ResponseDecoderPtr response_decoder_;
    absl::optional<Router::RouteConstSharedPtr> cached_route_;
    Buffer::OwnedImpl response_buffer_;
    int32_t original_sequence_id_{0};
  };

  typedef std::unique_ptr<ActiveRpc> ActiveRpcPtr;

  void continueDecoding();
  void dispatch();
  void sendLocalReply(MessageMetadata& metadata, const DirectResponse& reponse);
  void doDeferredRpcDestroy(ActiveRpc& rpc);
  void resetAllRpcs();

  Config& config_;
  ThriftFilterStats& stats_;

  Network::ReadFilterCallbacks* read_callbacks_{};

  TransportPtr transport_;
  ProtocolPtr protocol_;
  DecoderPtr decoder_;
  std::list<ActiveRpcPtr> rpcs_;
  Buffer::OwnedImpl request_buffer_;
  Runtime::RandomGenerator& random_generator_;
  bool stopped_{false};
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
