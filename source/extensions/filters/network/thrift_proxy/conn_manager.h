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

#include "extensions/filters/network/thrift_proxy/decoder.h"
#include "extensions/filters/network/thrift_proxy/filters/filter.h"
#include "extensions/filters/network/thrift_proxy/protocol.h"
#include "extensions/filters/network/thrift_proxy/protocol_converter.h"
#include "extensions/filters/network/thrift_proxy/stats.h"
#include "extensions/filters/network/thrift_proxy/transport.h"

#include "absl/types/any.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * Config is a configuration interface for ConnectionManager.
 */
class Config {
public:
  virtual ~Config() = default;

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
  ~ProtocolOptionsConfig() override = default;

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
  ConnectionManager(Config& config, Random::RandomGenerator& random_generator,
                    TimeSource& time_system);
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
    FilterStatus messageEnd() override;
    FilterStatus fieldBegin(absl::string_view name, FieldType& field_type,
                            int16_t& field_id) override;
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
  using ResponseDecoderPtr = std::unique_ptr<ResponseDecoder>;

  // Wraps a DecoderFilter and acts as the DecoderFilterCallbacks for the filter, enabling filter
  // chain continuation.
  struct ActiveRpcDecoderFilter : public ThriftFilters::DecoderFilterCallbacks,
                                  LinkedObject<ActiveRpcDecoderFilter> {
    ActiveRpcDecoderFilter(ActiveRpc& parent, ThriftFilters::DecoderFilterSharedPtr filter)
        : parent_(parent), handle_(filter) {}

    // ThriftFilters::DecoderFilterCallbacks
    uint64_t streamId() const override { return parent_.stream_id_; }
    const Network::Connection* connection() const override { return parent_.connection(); }
    void continueDecoding() override;
    Router::RouteConstSharedPtr route() override { return parent_.route(); }
    TransportType downstreamTransportType() const override {
      return parent_.downstreamTransportType();
    }
    ProtocolType downstreamProtocolType() const override {
      return parent_.downstreamProtocolType();
    }
    void sendLocalReply(const DirectResponse& response, bool end_stream) override {
      parent_.sendLocalReply(response, end_stream);
    }
    void startUpstreamResponse(Transport& transport, Protocol& protocol) override {
      parent_.startUpstreamResponse(transport, protocol);
    }
    ThriftFilters::ResponseStatus upstreamData(Buffer::Instance& buffer) override {
      return parent_.upstreamData(buffer);
    }
    void resetDownstreamConnection() override { parent_.resetDownstreamConnection(); }
    StreamInfo::StreamInfo& streamInfo() override { return parent_.streamInfo(); }

    ActiveRpc& parent_;
    ThriftFilters::DecoderFilterSharedPtr handle_;
  };
  using ActiveRpcDecoderFilterPtr = std::unique_ptr<ActiveRpcDecoderFilter>;

  // ActiveRpc tracks request/response pairs.
  struct ActiveRpc : LinkedObject<ActiveRpc>,
                     public Event::DeferredDeletable,
                     public DecoderEventHandler,
                     public ThriftFilters::DecoderFilterCallbacks,
                     public ThriftFilters::FilterChainFactoryCallbacks {
    ActiveRpc(ConnectionManager& parent)
        : parent_(parent), request_timer_(new Stats::HistogramCompletableTimespanImpl(
                               parent_.stats_.request_time_ms_, parent_.time_source_)),
          stream_id_(parent_.random_generator_.random()),
          stream_info_(parent_.time_source_), local_response_sent_{false}, pending_transport_end_{
                                                                               false} {
      parent_.stats_.request_active_.inc();

      stream_info_.setDownstreamLocalAddress(parent_.read_callbacks_->connection().localAddress());
      stream_info_.setDownstreamRemoteAddress(
          parent_.read_callbacks_->connection().remoteAddress());
      stream_info_.setDownstreamDirectRemoteAddress(
          parent_.read_callbacks_->connection().directRemoteAddress());
    }
    ~ActiveRpc() override {
      request_timer_->complete();
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
    FilterStatus structBegin(absl::string_view name) override;
    FilterStatus structEnd() override;
    FilterStatus fieldBegin(absl::string_view name, FieldType& field_type,
                            int16_t& field_id) override;
    FilterStatus fieldEnd() override;
    FilterStatus boolValue(bool& value) override;
    FilterStatus byteValue(uint8_t& value) override;
    FilterStatus int16Value(int16_t& value) override;
    FilterStatus int32Value(int32_t& value) override;
    FilterStatus int64Value(int64_t& value) override;
    FilterStatus doubleValue(double& value) override;
    FilterStatus stringValue(absl::string_view value) override;
    FilterStatus mapBegin(FieldType& key_type, FieldType& value_type, uint32_t& size) override;
    FilterStatus mapEnd() override;
    FilterStatus listBegin(FieldType& elem_type, uint32_t& size) override;
    FilterStatus listEnd() override;
    FilterStatus setBegin(FieldType& elem_type, uint32_t& size) override;
    FilterStatus setEnd() override;

    // ThriftFilters::DecoderFilterCallbacks
    uint64_t streamId() const override { return stream_id_; }
    const Network::Connection* connection() const override;
    void continueDecoding() override { parent_.continueDecoding(); }
    Router::RouteConstSharedPtr route() override;
    TransportType downstreamTransportType() const override {
      return parent_.decoder_->transportType();
    }
    ProtocolType downstreamProtocolType() const override {
      return parent_.decoder_->protocolType();
    }
    void sendLocalReply(const DirectResponse& response, bool end_stream) override;
    void startUpstreamResponse(Transport& transport, Protocol& protocol) override;
    ThriftFilters::ResponseStatus upstreamData(Buffer::Instance& buffer) override;
    void resetDownstreamConnection() override;
    StreamInfo::StreamInfo& streamInfo() override { return stream_info_; }

    // Thrift::FilterChainFactoryCallbacks
    void addDecoderFilter(ThriftFilters::DecoderFilterSharedPtr filter) override {
      ActiveRpcDecoderFilterPtr wrapper = std::make_unique<ActiveRpcDecoderFilter>(*this, filter);
      filter->setDecoderFilterCallbacks(*wrapper);
      wrapper->moveIntoListBack(std::move(wrapper), decoder_filters_);
    }

    FilterStatus applyDecoderFilters(ActiveRpcDecoderFilter* filter);
    void finalizeRequest();

    void createFilterChain();
    void onReset();
    void onError(const std::string& what);

    ConnectionManager& parent_;
    Stats::TimespanPtr request_timer_;
    uint64_t stream_id_;
    StreamInfo::StreamInfoImpl stream_info_;
    MessageMetadataSharedPtr metadata_;
    std::list<ActiveRpcDecoderFilterPtr> decoder_filters_;
    DecoderEventHandlerSharedPtr upgrade_handler_;
    ResponseDecoderPtr response_decoder_;
    absl::optional<Router::RouteConstSharedPtr> cached_route_;
    Buffer::OwnedImpl response_buffer_;
    int32_t original_sequence_id_{0};
    MessageType original_msg_type_{MessageType::Call};
    std::function<FilterStatus(DecoderEventHandler*)> filter_action_;
    absl::any filter_context_;
    bool local_response_sent_ : 1;
    bool pending_transport_end_ : 1;
  };

  using ActiveRpcPtr = std::unique_ptr<ActiveRpc>;

  void continueDecoding();
  void dispatch();
  void sendLocalReply(MessageMetadata& metadata, const DirectResponse& response, bool end_stream);
  void doDeferredRpcDestroy(ActiveRpc& rpc);
  void resetAllRpcs(bool local_reset);

  Config& config_;
  ThriftFilterStats& stats_;

  Network::ReadFilterCallbacks* read_callbacks_{};

  TransportPtr transport_;
  ProtocolPtr protocol_;
  DecoderPtr decoder_;
  std::list<ActiveRpcPtr> rpcs_;
  Buffer::OwnedImpl request_buffer_;
  Random::RandomGenerator& random_generator_;
  bool stopped_{false};
  bool half_closed_{false};
  TimeSource& time_source_;
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
