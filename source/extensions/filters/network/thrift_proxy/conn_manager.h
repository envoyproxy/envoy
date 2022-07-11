#pragma once

#include "envoy/common/pure.h"
#include "envoy/common/random_generator.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/network/connection.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"
#include "envoy/stats/timespan.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"
#include "source/common/stats/timespan_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/network/thrift_proxy/decoder.h"
#include "source/extensions/filters/network/thrift_proxy/filter_utils.h"
#include "source/extensions/filters/network/thrift_proxy/filters/filter.h"
#include "source/extensions/filters/network/thrift_proxy/protocol.h"
#include "source/extensions/filters/network/thrift_proxy/protocol_converter.h"
#include "source/extensions/filters/network/thrift_proxy/stats.h"
#include "source/extensions/filters/network/thrift_proxy/transport.h"

#include "absl/types/variant.h"

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
  virtual bool payloadPassthrough() const PURE;
  virtual uint64_t maxRequestsPerConnection() const PURE;
  virtual const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() const PURE;
  virtual bool headerKeysPreserveCase() const PURE;
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
                    TimeSource& time_system, const Network::DrainDecision& drain_decision);
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
  bool passthroughEnabled() const override;
  bool isRequest() const override { return true; }
  bool headerKeysPreserveCase() const override;

  using FilterContext =
      absl::variant<absl::monostate, MessageMetadataSharedPtr, Buffer::Instance*,
                    std::tuple<std::string, FieldType, int16_t>, bool, uint8_t, int16_t, int32_t,
                    int64_t, double, std::string, std::tuple<FieldType, FieldType, uint32_t>,
                    std::tuple<FieldType, uint32_t>>;

private:
  struct ActiveRpc;

  struct ResponseDecoder : public DecoderCallbacks, public DecoderEventHandler {
    ResponseDecoder(ActiveRpc& parent, Transport& transport, Protocol& protocol)
        : parent_(parent), decoder_(std::make_unique<Decoder>(transport, protocol, *this)),
          protocol_converter_(std::make_shared<ProtocolConverter>()), complete_{false},
          passthrough_{false}, pending_transport_end_{false} {
      protocol_converter_->initProtocolConverter(*parent_.parent_.protocol_,
                                                 parent_.response_buffer_);
    }

    bool onData(Buffer::Instance& data);

    // DecoderEventHandler
    FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override;
    FilterStatus transportEnd() override;
    FilterStatus passthroughData(Buffer::Instance& data) override;
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

    // DecoderCallbacks
    DecoderEventHandler& newDecoderEventHandler() override { return *this; }
    bool passthroughEnabled() const override;
    bool isRequest() const override { return false; }
    bool headerKeysPreserveCase() const override;

    void finalizeResponse();

    ActiveRpc& parent_;
    DecoderPtr decoder_;
    Buffer::OwnedImpl upstream_buffer_;
    MessageMetadataSharedPtr metadata_;
    ProtocolConverterSharedPtr protocol_converter_;
    absl::optional<bool> success_;
    bool complete_ : 1;
    bool passthrough_ : 1;
    bool pending_transport_end_ : 1;
  };
  using ResponseDecoderPtr = std::unique_ptr<ResponseDecoder>;

  struct ActiveRpcFilterBase : public virtual ThriftFilters::FilterCallbacks {
    ActiveRpcFilterBase(ActiveRpc& parent) : parent_(parent) {}

    // ThriftFilters::FilterCallbacks
    uint64_t streamId() const override { return parent_.stream_id_; }
    const Network::Connection* connection() const override { return parent_.connection(); }
    Event::Dispatcher& dispatcher() override { return parent_.dispatcher(); }
    Router::RouteConstSharedPtr route() override { return parent_.route(); }
    TransportType downstreamTransportType() const override {
      return parent_.downstreamTransportType();
    }
    ProtocolType downstreamProtocolType() const override {
      return parent_.downstreamProtocolType();
    }

    void resetDownstreamConnection() override { parent_.resetDownstreamConnection(); }
    StreamInfo::StreamInfo& streamInfo() override { return parent_.streamInfo(); }
    MessageMetadataSharedPtr responseMetadata() override { return parent_.responseMetadata(); }
    bool responseSuccess() override { return parent_.responseSuccess(); }
    void onReset() override { parent_.onReset(); }

    ActiveRpc& parent_;
  };

  // Wraps a DecoderFilter and acts as the DecoderFilterCallbacks for the filter, enabling filter
  // chain continuation.
  struct ActiveRpcDecoderFilter : public ActiveRpcFilterBase,
                                  public virtual ThriftFilters::DecoderFilterCallbacks,
                                  LinkedObject<ActiveRpcDecoderFilter> {
    ActiveRpcDecoderFilter(ActiveRpc& parent, ThriftFilters::DecoderFilterSharedPtr filter)
        : ActiveRpcFilterBase(parent), decoder_handle_(filter) {}

    // ThriftFilters::DecoderFilterCallbacks
    void sendLocalReply(const DirectResponse& response, bool end_stream) override {
      parent_.sendLocalReply(response, end_stream);
    }
    void startUpstreamResponse(Transport& transport, Protocol& protocol) override {
      parent_.startUpstreamResponse(transport, protocol);
    }
    ThriftFilters::ResponseStatus upstreamData(Buffer::Instance& buffer) override {
      return parent_.upstreamData(buffer);
    }
    void continueDecoding() override;
    DecoderEventHandler* decodeEventHandler() { return decoder_handle_.get(); }
    ThriftFilters::DecoderFilterSharedPtr decoder_handle_;
  };
  using ActiveRpcDecoderFilterPtr = std::unique_ptr<ActiveRpcDecoderFilter>;

  // Wraps a EncoderFilter and acts as the EncoderFilterCallbacks for the filter, enabling filter
  // chain continuation.
  struct ActiveRpcEncoderFilter : public ActiveRpcFilterBase,
                                  public virtual ThriftFilters::EncoderFilterCallbacks,
                                  LinkedObject<ActiveRpcEncoderFilter> {
    ActiveRpcEncoderFilter(ActiveRpc& parent, ThriftFilters::EncoderFilterSharedPtr filter)
        : ActiveRpcFilterBase(parent), encoder_handle_(filter) {}

    // ThriftFilters::EncoderFilterCallbacks
    void continueEncoding() override;
    DecoderEventHandler* decodeEventHandler() { return encoder_handle_.get(); }
    ThriftFilters::EncoderFilterSharedPtr encoder_handle_;
  };
  using ActiveRpcEncoderFilterPtr = std::unique_ptr<ActiveRpcEncoderFilter>;

  // ActiveRpc tracks request/response pairs.
  struct ActiveRpc : LinkedObject<ActiveRpc>,
                     public Event::DeferredDeletable,
                     public DecoderEventHandler,
                     public ThriftFilters::DecoderFilterCallbacks,
                     public ThriftFilters::EncoderFilterCallbacks,
                     public ThriftFilters::FilterChainFactoryCallbacks {
    ActiveRpc(ConnectionManager& parent)
        : parent_(parent), request_timer_(new Stats::HistogramCompletableTimespanImpl(
                               parent_.stats_.request_time_ms_, parent_.time_source_)),
          stream_id_(parent_.random_generator_.random()),
          stream_info_(parent_.time_source_,
                       parent_.read_callbacks_->connection().connectionInfoProviderSharedPtr()),
          local_response_sent_{false}, pending_transport_end_{false}, passthrough_{false},
          under_on_local_reply_{false} {
      parent_.stats_.request_active_.inc();
    }
    ~ActiveRpc() override {
      request_timer_->complete();
      stream_info_.onRequestComplete();
      parent_.stats_.request_active_.dec();

      parent_.emitLogEntry(metadata_ ? &metadata_->requestHeaders() : nullptr,
                           response_decoder_ && response_decoder_->metadata_
                               ? &response_decoder_->metadata_->responseHeaders()
                               : nullptr,
                           stream_info_);

      for (auto& filter : base_filters_) {
        filter->onDestroy();
      }
    }

    // DecoderEventHandler
    FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override;
    FilterStatus transportEnd() override;
    FilterStatus passthroughData(Buffer::Instance& data) override;
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
    Event::Dispatcher& dispatcher() override {
      return parent_.read_callbacks_->connection().dispatcher();
    }
    void continueDecoding() override { parent_.continueDecoding(); }
    void continueEncoding() override {}
    Router::RouteConstSharedPtr route() override;
    TransportType downstreamTransportType() const override {
      return parent_.decoder_->transportType();
    }
    ProtocolType downstreamProtocolType() const override {
      return parent_.decoder_->protocolType();
    }
    void onLocalReply(const MessageMetadata& metadata, bool end_stream);
    void sendLocalReply(const DirectResponse& response, bool end_stream) override;
    void startUpstreamResponse(Transport& transport, Protocol& protocol) override;
    ThriftFilters::ResponseStatus upstreamData(Buffer::Instance& buffer) override;
    void resetDownstreamConnection() override;
    StreamInfo::StreamInfo& streamInfo() override { return stream_info_; }
    MessageMetadataSharedPtr responseMetadata() override {
      ASSERT(localReplyMetadata_ || response_decoder_);
      return localReplyMetadata_ ? localReplyMetadata_ : response_decoder_->metadata_;
    }
    bool responseSuccess() override { return response_decoder_->success_.value_or(false); }
    void onReset() override;

    // Thrift::FilterChainFactoryCallbacks
    void addDecoderFilter(ThriftFilters::DecoderFilterSharedPtr filter) override {
      ActiveRpcDecoderFilterPtr wrapper = std::make_unique<ActiveRpcDecoderFilter>(*this, filter);
      filter->setDecoderFilterCallbacks(*wrapper);
      LinkedList::moveIntoListBack(std::move(wrapper), decoder_filters_);
      base_filters_.emplace_back(filter);
    }

    void addEncoderFilter(ThriftFilters::EncoderFilterSharedPtr filter) override {
      ActiveRpcEncoderFilterPtr wrapper = std::make_unique<ActiveRpcEncoderFilter>(*this, filter);
      filter->setEncoderFilterCallbacks(*wrapper);
      LinkedList::moveIntoList(std::move(wrapper), encoder_filters_);
      base_filters_.emplace_back(filter);
    }

    void addBidirectionalFilter(ThriftFilters::BidirectionalFilterSharedPtr filter) override {
      ThriftFilters::BidirectionalFilterWrapperSharedPtr wrapper =
          std::make_unique<ThriftFilters::BidirectionalFilterWrapper>(filter);

      ActiveRpcDecoderFilterPtr decoder_wrapper =
          std::make_unique<ActiveRpcDecoderFilter>(*this, wrapper->decoder_filter_);
      filter->setDecoderFilterCallbacks(*decoder_wrapper);
      LinkedList::moveIntoListBack(std::move(decoder_wrapper), decoder_filters_);

      ActiveRpcEncoderFilterPtr encoder_wrapper =
          std::make_unique<ActiveRpcEncoderFilter>(*this, wrapper->encoder_filter_);
      filter->setEncoderFilterCallbacks(*encoder_wrapper);
      LinkedList::moveIntoList(std::move(encoder_wrapper), encoder_filters_);

      base_filters_.emplace_back(wrapper);
    }

    bool passthroughSupported() const;

    // Apply filters to the decoder_event.
    // @param filter    the last filter which is already applied to the decoder_event.
    //                  nullptr indicates none is applied and the decoder_event is applied from the
    //                  first filter.
    FilterStatus applyDecoderFilters(DecoderEvent state, FilterContext&& data,
                                     ActiveRpcDecoderFilter* filter = nullptr);
    FilterStatus applyEncoderFilters(DecoderEvent state, FilterContext&& data,
                                     ProtocolConverterSharedPtr protocol_converter,
                                     ActiveRpcEncoderFilter* filter = nullptr);
    template <typename FilterType>
    FilterStatus applyFilters(FilterType* filter,
                              std::list<std::unique_ptr<FilterType>>& filter_list,
                              ProtocolConverterSharedPtr protocol_converter = nullptr);

    // Helper to setup filter_action_
    void prepareFilterAction(DecoderEvent event, FilterContext&& data);

    void finalizeRequest();

    void createFilterChain();
    void onError(const std::string& what);

    ConnectionManager& parent_;
    Stats::TimespanPtr request_timer_;
    uint64_t stream_id_;
    StreamInfo::StreamInfoImpl stream_info_;
    MessageMetadataSharedPtr metadata_;
    MessageMetadataSharedPtr localReplyMetadata_;
    std::list<ActiveRpcDecoderFilterPtr> decoder_filters_;
    std::list<ActiveRpcEncoderFilterPtr> encoder_filters_;
    std::list<ThriftFilters::FilterBaseSharedPtr> base_filters_;
    DecoderEventHandlerSharedPtr upgrade_handler_;
    ResponseDecoderPtr response_decoder_;
    absl::optional<Router::RouteConstSharedPtr> cached_route_;
    Buffer::OwnedImpl response_buffer_;
    int32_t original_sequence_id_{0};
    MessageType original_msg_type_{MessageType::Call};
    std::function<FilterStatus(DecoderEventHandler*)> filter_action_;
    bool local_response_sent_ : 1;
    bool pending_transport_end_ : 1;
    bool passthrough_ : 1;
    bool under_on_local_reply_ : 1;
  };

  using ActiveRpcPtr = std::unique_ptr<ActiveRpc>;

  void continueDecoding();
  void dispatch();
  void sendLocalReply(MessageMetadata& metadata, const DirectResponse& response, bool end_stream);
  void doDeferredRpcDestroy(ActiveRpc& rpc);
  void resetAllRpcs(bool local_reset);
  void emitLogEntry(const Http::RequestHeaderMap* request_headers,
                    const Http::ResponseHeaderMap* response_headers,
                    const StreamInfo::StreamInfo& stream_info);

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
  const Network::DrainDecision& drain_decision_;

  // The number of requests accumulated on the current connection.
  uint64_t accumulated_requests_{};
  bool requests_overflow_{false};
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
