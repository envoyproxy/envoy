#pragma once

#include "envoy/common/pure.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
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
  virtual DecoderPtr createDecoder(DecoderCallbacks& callbacks) PURE;
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
  ConnectionManager(Config& config);
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
  ThriftFilters::DecoderFilter& newDecoderFilter() override;

private:
  struct ActiveRpc;

  struct ResponseDecoder : public DecoderCallbacks, public ProtocolConverter {
    ResponseDecoder(ActiveRpc& parent, TransportType transport_type, ProtocolType protocol_type)
        : parent_(parent),
          decoder_(std::make_unique<Decoder>(
              NamedTransportConfigFactory::getFactory(transport_type).createTransport(),
              NamedProtocolConfigFactory::getFactory(protocol_type).createProtocol(), *this)),
          complete_(false), first_reply_field_(false) {
      // Use the factory to get the concrete protocol from the decoder protocol (as opposed to
      // potentially pre-detection auto protocol).
      initProtocolConverter(
          NamedProtocolConfigFactory::getFactory(parent_.parent_.decoder_->protocolType())
              .createProtocol(),
          parent_.response_buffer_);
    }

    bool onData(Buffer::Instance& data);

    // ProtocolConverter
    ThriftFilters::FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;
    ThriftFilters::FilterStatus fieldBegin(absl::string_view name, FieldType field_type,
                                           int16_t field_id) override;
    ThriftFilters::FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override {
      UNREFERENCED_PARAMETER(metadata);
      return ThriftFilters::FilterStatus::Continue;
    }
    ThriftFilters::FilterStatus transportEnd() override;

    // DecoderCallbacks
    ThriftFilters::DecoderFilter& newDecoderFilter() override { return *this; }

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
                     public ThriftFilters::DecoderFilter,
                     public ThriftFilters::DecoderFilterCallbacks,
                     public ThriftFilters::FilterChainFactoryCallbacks {
    ActiveRpc(ConnectionManager& parent)
        : parent_(parent), request_timer_(new Stats::Timespan(parent_.stats_.request_time_ms_)),
          stream_id_(parent_.stream_id_++) {
      parent_.stats_.request_active_.inc();
    }
    ~ActiveRpc() {
      request_timer_->complete();
      parent_.stats_.request_active_.dec();

      if (decoder_filter_ != nullptr) {
        decoder_filter_->onDestroy();
      }
    }

    // ThriftFilters::DecoderFilter
    void onDestroy() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
    void setDecoderFilterCallbacks(ThriftFilters::DecoderFilterCallbacks&) override {
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }
    void resetUpstreamConnection() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
    ThriftFilters::FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override {
      return decoder_filter_->transportBegin(metadata);
    }
    ThriftFilters::FilterStatus transportEnd() override;
    ThriftFilters::FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override {
      metadata_ = metadata;
      return decoder_filter_->messageBegin(metadata);
    }
    ThriftFilters::FilterStatus messageEnd() override { return decoder_filter_->messageEnd(); }
    ThriftFilters::FilterStatus structBegin(absl::string_view name) override {
      return decoder_filter_->structBegin(name);
    }
    ThriftFilters::FilterStatus structEnd() override { return decoder_filter_->structEnd(); }
    ThriftFilters::FilterStatus fieldBegin(absl::string_view name, FieldType field_type,
                                           int16_t field_id) override {
      return decoder_filter_->fieldBegin(name, field_type, field_id);
    }
    ThriftFilters::FilterStatus fieldEnd() override { return decoder_filter_->fieldEnd(); }
    ThriftFilters::FilterStatus boolValue(bool value) override {
      return decoder_filter_->boolValue(value);
    }
    ThriftFilters::FilterStatus byteValue(uint8_t value) override {
      return decoder_filter_->byteValue(value);
    }
    ThriftFilters::FilterStatus int16Value(int16_t value) override {
      return decoder_filter_->int16Value(value);
    }
    ThriftFilters::FilterStatus int32Value(int32_t value) override {
      return decoder_filter_->int32Value(value);
    }
    ThriftFilters::FilterStatus int64Value(int64_t value) override {
      return decoder_filter_->int64Value(value);
    }
    ThriftFilters::FilterStatus doubleValue(double value) override {
      return decoder_filter_->doubleValue(value);
    }
    ThriftFilters::FilterStatus stringValue(absl::string_view value) override {
      return decoder_filter_->stringValue(value);
    }
    ThriftFilters::FilterStatus mapBegin(FieldType key_type, FieldType value_type,
                                         uint32_t size) override {
      return decoder_filter_->mapBegin(key_type, value_type, size);
    }
    ThriftFilters::FilterStatus mapEnd() override { return decoder_filter_->mapEnd(); }
    ThriftFilters::FilterStatus listBegin(FieldType elem_type, uint32_t size) override {
      return decoder_filter_->listBegin(elem_type, size);
    }
    ThriftFilters::FilterStatus listEnd() override { return decoder_filter_->listEnd(); }
    ThriftFilters::FilterStatus setBegin(FieldType elem_type, uint32_t size) override {
      return decoder_filter_->setBegin(elem_type, size);
    }
    ThriftFilters::FilterStatus setEnd() override { return decoder_filter_->setEnd(); }

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
    void startUpstreamResponse(TransportType transport_type, ProtocolType protocol_type) override;
    bool upstreamData(Buffer::Instance& buffer) override;
    void resetDownstreamConnection() override;

    // Thrift::FilterChainFactoryCallbacks
    void addDecoderFilter(ThriftFilters::DecoderFilterSharedPtr filter) override {
      // TODO(zuercher): support multiple filters
      filter->setDecoderFilterCallbacks(*this);
      decoder_filter_ = filter;
    }

    void createFilterChain();
    void onReset();
    void onError(const std::string& what);

    ConnectionManager& parent_;
    Stats::TimespanPtr request_timer_;
    uint64_t stream_id_;
    MessageMetadataSharedPtr metadata_;
    ThriftFilters::DecoderFilterSharedPtr decoder_filter_;
    ResponseDecoderPtr response_decoder_;
    absl::optional<Router::RouteConstSharedPtr> cached_route_;
    Buffer::OwnedImpl response_buffer_;
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

  DecoderPtr decoder_;
  std::list<ActiveRpcPtr> rpcs_;
  Buffer::OwnedImpl request_buffer_;
  uint64_t stream_id_{1};
  bool stopped_{false};
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
