#include "extensions/filters/network/dubbo_proxy/active_message.h"

#include "extensions/filters/network/dubbo_proxy/app_exception.h"
#include "extensions/filters/network/dubbo_proxy/conn_manager.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

// class ResponseDecoder
ResponseDecoder::ResponseDecoder(Buffer::Instance& buffer, DubboFilterStats& stats,
                                 Network::Connection& connection, Deserializer& deserializer,
                                 Protocol& protocol)
    : response_buffer_(buffer), stats_(stats), response_connection_(connection),
      decoder_(std::make_unique<Decoder>(protocol, deserializer, *this)), complete_(false) {}

bool ResponseDecoder::onData(Buffer::Instance& data) {
  ENVOY_LOG(debug, "dubbo response: the received reply data length is {}", data.length());

  bool underflow = false;
  decoder_->onData(data, underflow);
  ASSERT(complete_ || underflow);
  return complete_;
}

Network::FilterStatus ResponseDecoder::transportBegin() {
  stats_.response_.inc();
  response_buffer_.drain(response_buffer_.length());
  ProtocolDataPassthroughConverter::initProtocolConverter(response_buffer_);

  return Network::FilterStatus::Continue;
}

Network::FilterStatus ResponseDecoder::transportEnd() {
  if (response_connection_.state() != Network::Connection::State::Open) {
    throw DownstreamConnectionCloseException("Downstream has closed or closing");
  }

  response_connection_.write(response_buffer_, false);
  ENVOY_LOG(debug,
            "dubbo response: the upstream response message has been forwarded to the downstream");
  return Network::FilterStatus::Continue;
}

Network::FilterStatus ResponseDecoder::messageBegin(MessageType, int64_t, SerializationType) {
  return Network::FilterStatus::Continue;
}

Network::FilterStatus ResponseDecoder::messageEnd(MessageMetadataSharedPtr metadata) {
  ASSERT(metadata->message_type() == MessageType::Response ||
         metadata->message_type() == MessageType::Exception);
  ASSERT(metadata->response_status().has_value());

  stats_.response_decoding_success_.inc();
  if (metadata->message_type() == MessageType::Exception) {
    stats_.response_business_exception_.inc();
  }

  metadata_ = metadata;
  switch (metadata->response_status().value()) {
  case ResponseStatus::Ok:
    stats_.response_success_.inc();
    break;
  default:
    stats_.response_error_.inc();
    ENVOY_LOG(error, "dubbo response status: {}",
              static_cast<uint8_t>(metadata->response_status().value()));
    break;
  }

  complete_ = true;
  ENVOY_LOG(debug, "dubbo response: complete processing of upstream response messages, id is {}",
            metadata->request_id());

  return Network::FilterStatus::Continue;
}

DecoderEventHandler* ResponseDecoder::newDecoderEventHandler() { return this; }

// class ActiveMessageDecoderFilter
ActiveMessageDecoderFilter::ActiveMessageDecoderFilter(ActiveMessage& parent,
                                                       DubboFilters::DecoderFilterSharedPtr filter)
    : parent_(parent), handle_(filter) {}

uint64_t ActiveMessageDecoderFilter::requestId() const { return parent_.requestId(); }

uint64_t ActiveMessageDecoderFilter::streamId() const { return parent_.streamId(); }

const Network::Connection* ActiveMessageDecoderFilter::connection() const {
  return parent_.connection();
}

void ActiveMessageDecoderFilter::continueDecoding() {
  const Network::FilterStatus status = parent_.applyDecoderFilters(this);
  if (status == Network::FilterStatus::Continue) {
    // All filters have been executed for the current decoder state.
    if (parent_.pending_transport_end()) {
      // If the filter stack was paused during messageEnd, handle end-of-request details.
      parent_.finalizeRequest();
    }
    parent_.continueDecoding();
  }
}

Router::RouteConstSharedPtr ActiveMessageDecoderFilter::route() { return parent_.route(); }

SerializationType ActiveMessageDecoderFilter::downstreamSerializationType() const {
  return parent_.downstreamSerializationType();
}

ProtocolType ActiveMessageDecoderFilter::downstreamProtocolType() const {
  return parent_.downstreamProtocolType();
}

void ActiveMessageDecoderFilter::sendLocalReply(const DubboFilters::DirectResponse& response,
                                                bool end_stream) {
  parent_.sendLocalReply(response, end_stream);
}

void ActiveMessageDecoderFilter::startUpstreamResponse(Deserializer& deserializer,
                                                       Protocol& protocol) {
  parent_.startUpstreamResponse(deserializer, protocol);
}

DubboFilters::UpstreamResponseStatus
ActiveMessageDecoderFilter::upstreamData(Buffer::Instance& buffer) {
  return parent_.upstreamData(buffer);
}

void ActiveMessageDecoderFilter::resetDownstreamConnection() {
  parent_.resetDownstreamConnection();
}

void ActiveMessageDecoderFilter::resetStream() { parent_.resetStream(); }

StreamInfo::StreamInfo& ActiveMessageDecoderFilter::streamInfo() { return parent_.streamInfo(); }

// class ActiveMessage
ActiveMessage::ActiveMessage(ConnectionManager& parent)
    : parent_(parent), request_timer_(std::make_unique<Stats::Timespan>(
                           parent_.stats().request_time_ms_, parent.time_system())),
      request_id_(-1), stream_id_(parent.random_generator().random()),
      stream_info_(parent.time_system()), pending_transport_end_(false),
      local_response_sent_(false) {
  parent_.stats().request_active_.inc();
  stream_info_.setDownstreamLocalAddress(parent_.connection().localAddress());
  stream_info_.setDownstreamRemoteAddress(parent_.connection().remoteAddress());
}

ActiveMessage::~ActiveMessage() {
  parent_.stats().request_active_.dec();
  request_timer_->complete();
  for (auto& filter : decoder_filters_) {
    filter->handler()->onDestroy();
  }
  ENVOY_LOG(debug, "ActiveMessage::~ActiveMessage()");
}

Network::FilterStatus ActiveMessage::transportBegin() {
  filter_action_ = [](DubboFilters::DecoderFilter* filter) -> Network::FilterStatus {
    return filter->transportBegin();
  };

  return this->applyDecoderFilters(nullptr);
}

Network::FilterStatus ActiveMessage::transportEnd() {
  filter_action_ = [](DubboFilters::DecoderFilter* filter) -> Network::FilterStatus {
    return filter->transportEnd();
  };

  Network::FilterStatus status = applyDecoderFilters(nullptr);
  if (status == Network::FilterStatus::StopIteration) {
    pending_transport_end_ = true;
    return status;
  }

  finalizeRequest();

  ENVOY_LOG(debug, "dubbo request: complete processing of downstream request messages, id is {}",
            request_id_);

  return status;
}

Network::FilterStatus ActiveMessage::messageBegin(MessageType type, int64_t message_id,
                                                  SerializationType serialization_type) {
  request_id_ = message_id;
  filter_action_ = [type, message_id, serialization_type](
                       DubboFilters::DecoderFilter* filter) -> Network::FilterStatus {
    return filter->messageBegin(type, message_id, serialization_type);
  };

  return applyDecoderFilters(nullptr);
}

Network::FilterStatus ActiveMessage::messageEnd(MessageMetadataSharedPtr metadata) {
  ASSERT(metadata->message_type() == MessageType::Request ||
         metadata->message_type() == MessageType::Oneway);

  // Currently only hessian serialization is implemented.
  ASSERT(metadata->serialization_type() == SerializationType::Hessian);

  ENVOY_LOG(debug, "dubbo request: start processing downstream request messages, id is {}",
            metadata->request_id());

  parent_.stats().request_decoding_success_.inc();

  metadata_ = metadata;
  filter_action_ = [metadata](DubboFilters::DecoderFilter* filter) -> Network::FilterStatus {
    return filter->messageEnd(metadata);
  };

  return applyDecoderFilters(nullptr);
}

Network::FilterStatus ActiveMessage::transferHeaderTo(Buffer::Instance& header_buf, size_t size) {
  filter_action_ = [&header_buf,
                    size](DubboFilters::DecoderFilter* filter) -> Network::FilterStatus {
    return filter->transferHeaderTo(header_buf, size);
  };

  // If a local reply is generated, the filter callback is skipped and
  // the buffer data needs to be actively released.
  if (local_response_sent_) {
    header_buf.drain(size);
  }

  return applyDecoderFilters(nullptr);
}

Network::FilterStatus ActiveMessage::transferBodyTo(Buffer::Instance& body_buf, size_t size) {
  filter_action_ = [&body_buf, size](DubboFilters::DecoderFilter* filter) -> Network::FilterStatus {
    return filter->transferBodyTo(body_buf, size);
  };

  // If a local reply is generated, the filter callback is skipped and
  // the buffer data needs to be actively released.
  if (local_response_sent_) {
    body_buf.drain(size);
  }

  return applyDecoderFilters(nullptr);
}

void ActiveMessage::finalizeRequest() {
  pending_transport_end_ = false;
  parent_.stats().request_.inc();
  bool is_one_way = false;
  switch (metadata_->message_type()) {
  case MessageType::Request:
    parent_.stats().request_twoway_.inc();
    break;
  case MessageType::Oneway:
    parent_.stats().request_oneway_.inc();
    is_one_way = true;
    break;
  default:
    break;
  }

  if (local_response_sent_ || is_one_way) {
    parent_.deferredMessage(*this);
  }
}

void ActiveMessage::createFilterChain() {
  parent_.config().filterFactory().createFilterChain(*this);
}

DubboProxy::Router::RouteConstSharedPtr ActiveMessage::route() {
  if (cached_route_) {
    return cached_route_.value();
  }

  if (metadata_ != nullptr) {
    DubboProxy::Router::RouteConstSharedPtr route =
        parent_.config().routerConfig().route(*metadata_, stream_id_);
    cached_route_ = route;
    return cached_route_.value();
  }

  return nullptr;
}

Network::FilterStatus ActiveMessage::applyDecoderFilters(ActiveMessageDecoderFilter* filter) {
  ASSERT(filter_action_ != nullptr);

  if (!local_response_sent_) {
    std::list<ActiveMessageDecoderFilterPtr>::iterator entry;
    if (!filter) {
      entry = decoder_filters_.begin();
    } else {
      entry = std::next(filter->entry());
    }

    for (; entry != decoder_filters_.end(); entry++) {
      const Network::FilterStatus status = filter_action_((*entry)->handler().get());
      if (local_response_sent_) {
        break;
      }

      if (status != Network::FilterStatus::Continue) {
        return status;
      }
    }
  }

  filter_action_ = nullptr;

  return Network::FilterStatus::Continue;
}

void ActiveMessage::sendLocalReply(const DubboFilters::DirectResponse& response, bool end_stream) {
  if (!metadata_) {
    // If the sendLocalReply function is called before the messageEnd callback,
    // metadata_ is nullptr, metadata object needs to be created in order to generate a local reply.
    metadata_ = std::make_shared<MessageMetadata>();
  }
  metadata_->setRequestId(request_id_);
  parent_.sendLocalReply(*metadata_, response, end_stream);

  if (end_stream) {
    return;
  }

  local_response_sent_ = true;
}

void ActiveMessage::startUpstreamResponse(Deserializer& deserializer, Protocol& protocol) {
  ENVOY_LOG(debug, "dubbo response: start upstream");

  ASSERT(response_decoder_ == nullptr);

  // Create a response message decoder.
  response_decoder_ = std::make_unique<ResponseDecoder>(
      response_buffer_, parent_.stats(), parent_.connection(), deserializer, protocol);
}

DubboFilters::UpstreamResponseStatus ActiveMessage::upstreamData(Buffer::Instance& buffer) {
  ASSERT(response_decoder_ != nullptr);

  try {
    if (response_decoder_->onData(buffer)) {
      if (requestId() != response_decoder_->requestId()) {
        throw EnvoyException(fmt::format("dubbo response: request ID is not equal, {}:{}",
                                         requestId(), response_decoder_->requestId()));
      }

      // Completed upstream response.
      parent_.deferredMessage(*this);
      return DubboFilters::UpstreamResponseStatus::Complete;
    }
    return DubboFilters::UpstreamResponseStatus::MoreData;
  } catch (const DownstreamConnectionCloseException& ex) {
    ENVOY_CONN_LOG(error, "dubbo response: exception ({})", parent_.connection(), ex.what());
    onReset();
    parent_.stats().response_error_caused_connection_close_.inc();
    return DubboFilters::UpstreamResponseStatus::Reset;
  } catch (const EnvoyException& ex) {
    ENVOY_CONN_LOG(error, "dubbo response: exception ({})", parent_.connection(), ex.what());
    parent_.stats().response_decoding_error_.inc();

    onError(ex.what());
    return DubboFilters::UpstreamResponseStatus::Reset;
  }
}

void ActiveMessage::resetDownstreamConnection() {
  parent_.connection().close(Network::ConnectionCloseType::NoFlush);
}

void ActiveMessage::resetStream() { parent_.deferredMessage(*this); }

uint64_t ActiveMessage::requestId() const {
  return metadata_ != nullptr ? metadata_->request_id() : 0;
}

uint64_t ActiveMessage::streamId() const { return stream_id_; }

void ActiveMessage::continueDecoding() { parent_.continueDecoding(); }

SerializationType ActiveMessage::downstreamSerializationType() const {
  return parent_.downstreamSerializationType();
}

ProtocolType ActiveMessage::downstreamProtocolType() const {
  return parent_.downstreamProtocolType();
}

StreamInfo::StreamInfo& ActiveMessage::streamInfo() { return stream_info_; }

const Network::Connection* ActiveMessage::connection() const { return &parent_.connection(); }

void ActiveMessage::addDecoderFilter(DubboFilters::DecoderFilterSharedPtr filter) {
  ActiveMessageDecoderFilterPtr wrapper =
      std::make_unique<ActiveMessageDecoderFilter>(*this, filter);
  filter->setDecoderFilterCallbacks(*wrapper);
  wrapper->moveIntoListBack(std::move(wrapper), decoder_filters_);
}

void ActiveMessage::onReset() { parent_.deferredMessage(*this); }

void ActiveMessage::onError(const std::string& what) {
  if (!metadata_) {
    // It's possible that an error occurred before the decoder generated metadata,
    // and a metadata object needs to be created in order to generate a local reply.
    metadata_ = std::make_shared<MessageMetadata>();
  }

  ASSERT(metadata_);
  ENVOY_LOG(error, "Bad response: {}", what);
  sendLocalReply(AppException(ResponseStatus::BadResponse, what), false);
  parent_.deferredMessage(*this);
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
