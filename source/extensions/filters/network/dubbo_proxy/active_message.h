#pragma once

#include "envoy/event/deferred_deletable.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/timespan.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/linked_object.h"
#include "common/common/logger.h"
#include "common/stream_info/stream_info_impl.h"

#include "extensions/filters/network/dubbo_proxy/decoder.h"
#include "extensions/filters/network/dubbo_proxy/decoder_event_handler.h"
#include "extensions/filters/network/dubbo_proxy/filters/filter.h"
#include "extensions/filters/network/dubbo_proxy/metadata.h"
#include "extensions/filters/network/dubbo_proxy/router/router.h"
#include "extensions/filters/network/dubbo_proxy/stats.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class ConnectionManager;
class ActiveMessage;

class ResponseDecoder : public DecoderCallbacks,
                        public DecoderEventHandler,
                        Logger::Loggable<Logger::Id::dubbo> {
public:
  ResponseDecoder(Buffer::Instance& buffer, DubboFilterStats& stats,
                  Network::Connection& connection, Deserializer& deserializer, Protocol& protocol);
  ~ResponseDecoder() override = default;

  bool onData(Buffer::Instance& data);

  // DecoderEventHandler
  Network::FilterStatus transportBegin() override;
  Network::FilterStatus transportEnd() override;
  Network::FilterStatus messageBegin(MessageType type, int64_t message_id,
                                     SerializationType serialization_type) override;
  Network::FilterStatus messageEnd(MessageMetadataSharedPtr metadata) override;

  // DecoderCallbacks
  DecoderEventHandler* newDecoderEventHandler() override;

  uint64_t requestId() const { return metadata_ ? metadata_->request_id() : 0; }

private:
  Buffer::Instance& response_buffer_;
  DubboFilterStats& stats_;
  Network::Connection& response_connection_;
  DecoderPtr decoder_;
  MessageMetadataSharedPtr metadata_;
  bool complete_ : 1;
};

using ResponseDecoderPtr = std::unique_ptr<ResponseDecoder>;

// Wraps a DecoderFilter and acts as the DecoderFilterCallbacks for the filter, enabling filter
// chain continuation.
class ActiveMessageDecoderFilter : public DubboFilters::DecoderFilterCallbacks,
                                   public LinkedObject<ActiveMessageDecoderFilter> {
public:
  ActiveMessageDecoderFilter(ActiveMessage& parent, DubboFilters::DecoderFilterSharedPtr filter);
  ~ActiveMessageDecoderFilter() override = default;

  // DubboFilters::DecoderFilterCallbacks
  uint64_t requestId() const override;
  uint64_t streamId() const override;
  const Network::Connection* connection() const override;
  void continueDecoding() override;
  DubboProxy::Router::RouteConstSharedPtr route() override;
  SerializationType downstreamSerializationType() const override;
  ProtocolType downstreamProtocolType() const override;
  void sendLocalReply(const DubboFilters::DirectResponse& response, bool end_stream) override;
  void startUpstreamResponse(Deserializer& deserializer, Protocol& protocol) override;
  DubboFilters::UpstreamResponseStatus upstreamData(Buffer::Instance& buffer) override;
  void resetDownstreamConnection() override;
  StreamInfo::StreamInfo& streamInfo() override;
  void resetStream() override;

  DubboFilters::DecoderFilterSharedPtr handler() { return handle_; }

private:
  ActiveMessage& parent_;
  DubboFilters::DecoderFilterSharedPtr handle_;
};

using ActiveMessageDecoderFilterPtr = std::unique_ptr<ActiveMessageDecoderFilter>;

// ActiveMessage tracks downstream requests for which no response has been received.
class ActiveMessage : public LinkedObject<ActiveMessage>,
                      public Event::DeferredDeletable,
                      public DecoderEventHandler,
                      public DubboFilters::DecoderFilterCallbacks,
                      public DubboFilters::FilterChainFactoryCallbacks,
                      Logger::Loggable<Logger::Id::dubbo> {
public:
  ActiveMessage(ConnectionManager& parent);
  ~ActiveMessage() override;

  // Dubbo::FilterChainFactoryCallbacks
  void addDecoderFilter(DubboFilters::DecoderFilterSharedPtr filter) override;

  // DecoderEventHandler
  Network::FilterStatus transportBegin() override;
  Network::FilterStatus transportEnd() override;
  Network::FilterStatus messageBegin(MessageType type, int64_t message_id,
                                     SerializationType serialization_type) override;
  Network::FilterStatus messageEnd(MessageMetadataSharedPtr metadata) override;
  Network::FilterStatus transferHeaderTo(Buffer::Instance& header_buf, size_t size) override;
  Network::FilterStatus transferBodyTo(Buffer::Instance& body_buf, size_t size) override;

  // DubboFilters::DecoderFilterCallbacks
  uint64_t requestId() const override;
  uint64_t streamId() const override;
  const Network::Connection* connection() const override;
  void continueDecoding() override;
  SerializationType downstreamSerializationType() const override;
  ProtocolType downstreamProtocolType() const override;
  StreamInfo::StreamInfo& streamInfo() override;
  Router::RouteConstSharedPtr route() override;
  void sendLocalReply(const DubboFilters::DirectResponse& response, bool end_stream) override;
  void startUpstreamResponse(Deserializer& deserializer, Protocol& protocol) override;
  DubboFilters::UpstreamResponseStatus upstreamData(Buffer::Instance& buffer) override;
  void resetDownstreamConnection() override;
  void resetStream() override;

  void createFilterChain();
  Network::FilterStatus applyDecoderFilters(ActiveMessageDecoderFilter* filter);
  void finalizeRequest();
  void onReset();
  void onError(const std::string& what);
  MessageMetadataSharedPtr metadata() const { return metadata_; }
  bool pending_transport_end() const { return pending_transport_end_; }

private:
  ConnectionManager& parent_;

  MessageMetadataSharedPtr metadata_;
  Stats::TimespanPtr request_timer_;
  ResponseDecoderPtr response_decoder_;

  absl::optional<Router::RouteConstSharedPtr> cached_route_;

  std::list<ActiveMessageDecoderFilterPtr> decoder_filters_;
  std::function<Network::FilterStatus(DubboFilters::DecoderFilter*)> filter_action_;

  int32_t request_id_;

  // This value is used in the calculation of the weighted cluster.
  uint64_t stream_id_;
  StreamInfo::StreamInfoImpl stream_info_;

  Buffer::OwnedImpl response_buffer_;

  bool pending_transport_end_ : 1;
  bool local_response_sent_ : 1;
};

using ActiveMessagePtr = std::unique_ptr<ActiveMessage>;

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
