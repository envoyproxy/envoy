#pragma once

#include "envoy/event/deferred_deletable.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/timespan.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/decoder.h"
#include "source/extensions/filters/network/dubbo_proxy/decoder_event_handler.h"
#include "source/extensions/filters/network/dubbo_proxy/filters/filter.h"
#include "source/extensions/filters/network/dubbo_proxy/metadata.h"
#include "source/extensions/filters/network/dubbo_proxy/router/router.h"
#include "source/extensions/filters/network/dubbo_proxy/stats.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class ConnectionManager;
class ActiveMessage;

class ActiveResponseDecoder : public ResponseDecoderCallbacks,
                              public StreamHandler,
                              Logger::Loggable<Logger::Id::dubbo> {
public:
  ActiveResponseDecoder(ActiveMessage& parent, DubboFilterStats& stats,
                        Network::Connection& connection, ProtocolPtr&& protocol);
  ~ActiveResponseDecoder() override = default;

  DubboFilters::UpstreamResponseStatus onData(Buffer::Instance& data);

  // StreamHandler
  void onStreamDecoded(MessageMetadataSharedPtr metadata, ContextSharedPtr ctx) override;

  // ResponseDecoderCallbacks
  StreamHandler& newStream() override { return *this; }
  void onHeartbeat(MessageMetadataSharedPtr) override {}

  uint64_t requestId() const { return metadata_ ? metadata_->requestId() : 0; }

private:
  FilterStatus applyMessageEncodedFilters(MessageMetadataSharedPtr metadata, ContextSharedPtr ctx);

  ActiveMessage& parent_;
  DubboFilterStats& stats_;
  Network::Connection& response_connection_;
  ProtocolPtr protocol_;
  ResponseDecoderPtr decoder_;
  MessageMetadataSharedPtr metadata_;
  bool complete_ : 1;
  DubboFilters::UpstreamResponseStatus response_status_{
      DubboFilters::UpstreamResponseStatus::MoreData};
};

using ActiveResponseDecoderPtr = std::unique_ptr<ActiveResponseDecoder>;

class ActiveMessageFilterBase : public virtual DubboFilters::FilterCallbacksBase {
public:
  ActiveMessageFilterBase(ActiveMessage& parent, bool dual_filter)
      : parent_(parent), dual_filter_(dual_filter) {}
  ~ActiveMessageFilterBase() override = default;

  // DubboFilters::FilterCallbacksBase
  uint64_t requestId() const override;
  uint64_t streamId() const override;
  const Network::Connection* connection() const override;
  DubboProxy::Router::RouteConstSharedPtr route() override;
  SerializationType serializationType() const override;
  ProtocolType protocolType() const override;
  StreamInfo::StreamInfo& streamInfo() override;
  Event::Dispatcher& dispatcher() override;
  void resetStream() override;

protected:
  ActiveMessage& parent_;
  const bool dual_filter_ : 1;
};

// Wraps a DecoderFilter and acts as the DecoderFilterCallbacks for the filter, enabling filter
// chain continuation.
class ActiveMessageDecoderFilter : public DubboFilters::DecoderFilterCallbacks,
                                   public ActiveMessageFilterBase,
                                   public LinkedObject<ActiveMessageDecoderFilter>,
                                   Logger::Loggable<Logger::Id::dubbo> {
public:
  ActiveMessageDecoderFilter(ActiveMessage& parent, DubboFilters::DecoderFilterSharedPtr filter,
                             bool dual_filter);
  ~ActiveMessageDecoderFilter() override = default;

  void continueDecoding() override;
  void sendLocalReply(const DubboFilters::DirectResponse& response, bool end_stream) override;
  void startUpstreamResponse() override;
  DubboFilters::UpstreamResponseStatus upstreamData(Buffer::Instance& buffer) override;
  void resetDownstreamConnection() override;

  DubboFilters::DecoderFilterSharedPtr handler() { return handle_; }

private:
  DubboFilters::DecoderFilterSharedPtr handle_;
};

using ActiveMessageDecoderFilterPtr = std::unique_ptr<ActiveMessageDecoderFilter>;

// Wraps a EncoderFilter and acts as the EncoderFilterCallbacks for the filter, enabling filter
// chain continuation.
class ActiveMessageEncoderFilter : public ActiveMessageFilterBase,
                                   public DubboFilters::EncoderFilterCallbacks,
                                   public LinkedObject<ActiveMessageEncoderFilter>,
                                   Logger::Loggable<Logger::Id::dubbo> {
public:
  ActiveMessageEncoderFilter(ActiveMessage& parent, DubboFilters::EncoderFilterSharedPtr filter,
                             bool dual_filter);
  ~ActiveMessageEncoderFilter() override = default;

  void continueEncoding() override;
  DubboFilters::EncoderFilterSharedPtr handler() { return handle_; }

private:
  DubboFilters::EncoderFilterSharedPtr handle_;

  friend class ActiveMessage;
};

using ActiveMessageEncoderFilterPtr = std::unique_ptr<ActiveMessageEncoderFilter>;

// ActiveMessage tracks downstream requests for which no response has been received.
class ActiveMessage : public LinkedObject<ActiveMessage>,
                      public Event::DeferredDeletable,
                      public StreamHandler,
                      public DubboFilters::FilterChainFactoryCallbacks,
                      Logger::Loggable<Logger::Id::dubbo> {
public:
  ActiveMessage(ConnectionManager& parent);
  ~ActiveMessage() override;

  // Indicates which filter to start the iteration with.
  enum class FilterIterationStartState { AlwaysStartFromNext, CanStartFromCurrent };

  // Returns the encoder filter to start iteration with.
  std::list<ActiveMessageEncoderFilterPtr>::iterator
  commonEncodePrefix(ActiveMessageEncoderFilter* filter, FilterIterationStartState state);
  // Returns the decoder filter to start iteration with.
  std::list<ActiveMessageDecoderFilterPtr>::iterator
  commonDecodePrefix(ActiveMessageDecoderFilter* filter, FilterIterationStartState state);

  // Dubbo::FilterChainFactoryCallbacks
  void addDecoderFilter(DubboFilters::DecoderFilterSharedPtr filter) override;
  void addEncoderFilter(DubboFilters::EncoderFilterSharedPtr filter) override;
  void addFilter(DubboFilters::CodecFilterSharedPtr filter) override;

  // StreamHandler
  void onStreamDecoded(MessageMetadataSharedPtr metadata, ContextSharedPtr ctx) override;

  uint64_t requestId() const;
  uint64_t streamId() const;
  const Network::Connection* connection() const;
  SerializationType serializationType() const;
  ProtocolType protocolType() const;
  StreamInfo::StreamInfo& streamInfo();
  Router::RouteConstSharedPtr route();
  void sendLocalReply(const DubboFilters::DirectResponse& response, bool end_stream);
  void startUpstreamResponse();
  DubboFilters::UpstreamResponseStatus upstreamData(Buffer::Instance& buffer);
  void resetDownstreamConnection();
  Event::Dispatcher& dispatcher();
  void resetStream();

  void createFilterChain();
  FilterStatus applyDecoderFilters(ActiveMessageDecoderFilter* filter,
                                   FilterIterationStartState state);
  FilterStatus applyEncoderFilters(ActiveMessageEncoderFilter* filter,
                                   FilterIterationStartState state);
  void finalizeRequest();
  void onReset();
  void onError(const std::string& what);
  MessageMetadataSharedPtr metadata() const { return metadata_; }
  ContextSharedPtr context() const { return context_; }
  bool pendingStreamDecoded() const { return pending_stream_decoded_; }

private:
  void addDecoderFilterWorker(DubboFilters::DecoderFilterSharedPtr filter, bool dual_filter);
  void addEncoderFilterWorker(DubboFilters::EncoderFilterSharedPtr, bool dual_filter);

  ConnectionManager& parent_;

  ContextSharedPtr context_;
  MessageMetadataSharedPtr metadata_;
  Stats::TimespanPtr request_timer_;
  ActiveResponseDecoderPtr response_decoder_;

  absl::optional<Router::RouteConstSharedPtr> cached_route_;

  std::list<ActiveMessageDecoderFilterPtr> decoder_filters_;
  std::function<FilterStatus(DubboFilters::DecoderFilter*)> filter_action_;

  std::list<ActiveMessageEncoderFilterPtr> encoder_filters_;
  std::function<FilterStatus(DubboFilters::EncoderFilter*)> encoder_filter_action_;

  // This value is used in the calculation of the weighted cluster.
  uint64_t stream_id_;
  StreamInfo::StreamInfoImpl stream_info_;

  Buffer::OwnedImpl response_buffer_;

  bool pending_stream_decoded_ : 1;
  bool local_response_sent_ : 1;

  friend class ActiveResponseDecoder;
};

using ActiveMessagePtr = std::unique_ptr<ActiveMessage>;

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
