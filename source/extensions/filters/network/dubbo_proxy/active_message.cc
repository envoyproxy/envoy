#include "source/extensions/filters/network/dubbo_proxy/active_message.h"

#include "source/common/stats/timespan_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/app_exception.h"
#include "source/extensions/filters/network/dubbo_proxy/conn_manager.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

// class ActiveResponseDecoder
ActiveResponseDecoder::ActiveResponseDecoder(ActiveMessage& parent, DubboFilterStats& stats,
                                             Network::Connection& connection,
                                             ProtocolPtr&& protocol)
    : parent_(parent), stats_(stats), response_connection_(connection),
      protocol_(std::move(protocol)),
      decoder_(std::make_unique<ResponseDecoder>(*protocol_, *this)), complete_(false) {}

DubboFilters::UpstreamResponseStatus ActiveResponseDecoder::onData(Buffer::Instance& data) {
  ENVOY_LOG(debug, "dubbo response: the received reply data length is {}", data.length());

  bool underflow = false;
  decoder_->onData(data, underflow);
  ASSERT(complete_ || underflow);

  return response_status_;
}

void ActiveResponseDecoder::onStreamDecoded(MessageMetadataSharedPtr metadata,
                                            ContextSharedPtr ctx) {
  ASSERT(metadata->messageType() == MessageType::Response ||
         metadata->messageType() == MessageType::Exception);
  ASSERT(metadata->hasResponseStatus());

  metadata_ = metadata;
  if (applyMessageEncodedFilters(metadata, ctx) != FilterStatus::Continue) {
    response_status_ = DubboFilters::UpstreamResponseStatus::Complete;
    return;
  }

  if (response_connection_.state() != Network::Connection::State::Open) {
    throw DownstreamConnectionCloseException("Downstream has closed or closing");
  }

  response_connection_.write(ctx->originMessage(), false);
  ENVOY_LOG(debug,
            "dubbo response: the upstream response message has been forwarded to the downstream");

  stats_.response_.inc();
  stats_.response_decoding_success_.inc();
  if (metadata->messageType() == MessageType::Exception) {
    stats_.response_business_exception_.inc();
  }

  switch (metadata->responseStatus()) {
  case ResponseStatus::Ok:
    stats_.response_success_.inc();
    break;
  default:
    stats_.response_error_.inc();
    ENVOY_LOG(error, "dubbo response status: {}", static_cast<uint8_t>(metadata->responseStatus()));
    break;
  }

  complete_ = true;
  response_status_ = DubboFilters::UpstreamResponseStatus::Complete;

  ENVOY_LOG(debug, "dubbo response: complete processing of upstream response messages, id is {}",
            metadata->requestId());
}

FilterStatus ActiveResponseDecoder::applyMessageEncodedFilters(MessageMetadataSharedPtr metadata,
                                                               ContextSharedPtr ctx) {
  parent_.encoder_filter_action_ = [metadata,
                                    ctx](DubboFilters::EncoderFilter* filter) -> FilterStatus {
    return filter->onMessageEncoded(metadata, ctx);
  };

  auto status = parent_.applyEncoderFilters(
      nullptr, ActiveMessage::FilterIterationStartState::CanStartFromCurrent);
  switch (status) {
  case FilterStatus::StopIteration:
    break;
  default:
    ASSERT(FilterStatus::Continue == status);
    break;
  }

  return status;
}

// class ActiveMessageFilterBase
uint64_t ActiveMessageFilterBase::requestId() const { return parent_.requestId(); }

uint64_t ActiveMessageFilterBase::streamId() const { return parent_.streamId(); }

const Network::Connection* ActiveMessageFilterBase::connection() const {
  return parent_.connection();
}

Router::RouteConstSharedPtr ActiveMessageFilterBase::route() { return parent_.route(); }

SerializationType ActiveMessageFilterBase::serializationType() const {
  return parent_.serializationType();
}

ProtocolType ActiveMessageFilterBase::protocolType() const { return parent_.protocolType(); }

Event::Dispatcher& ActiveMessageFilterBase::dispatcher() { return parent_.dispatcher(); }

void ActiveMessageFilterBase::resetStream() { parent_.resetStream(); }

StreamInfo::StreamInfo& ActiveMessageFilterBase::streamInfo() { return parent_.streamInfo(); }

// class ActiveMessageDecoderFilter
ActiveMessageDecoderFilter::ActiveMessageDecoderFilter(ActiveMessage& parent,
                                                       DubboFilters::DecoderFilterSharedPtr filter,
                                                       bool dual_filter)
    : ActiveMessageFilterBase(parent, dual_filter), handle_(filter) {}

void ActiveMessageDecoderFilter::continueDecoding() {
  ASSERT(parent_.context());
  auto state = ActiveMessage::FilterIterationStartState::AlwaysStartFromNext;
  if (0 != parent_.context()->originMessage().length()) {
    state = ActiveMessage::FilterIterationStartState::CanStartFromCurrent;
    ENVOY_LOG(warn, "The original message data is not consumed, triggering the decoder filter from "
                    "the current location");
  }
  const FilterStatus status = parent_.applyDecoderFilters(this, state);
  if (status == FilterStatus::Continue) {
    ENVOY_LOG(debug, "dubbo response: start upstream");
    // All filters have been executed for the current decoder state.
    if (parent_.pendingStreamDecoded()) {
      // If the filter stack was paused during messageEnd, handle end-of-request details.
      parent_.finalizeRequest();
    }
  }
}

void ActiveMessageDecoderFilter::sendLocalReply(const DubboFilters::DirectResponse& response,
                                                bool end_stream) {
  parent_.sendLocalReply(response, end_stream);
}

void ActiveMessageDecoderFilter::startUpstreamResponse() { parent_.startUpstreamResponse(); }

DubboFilters::UpstreamResponseStatus
ActiveMessageDecoderFilter::upstreamData(Buffer::Instance& buffer) {
  return parent_.upstreamData(buffer);
}

void ActiveMessageDecoderFilter::resetDownstreamConnection() {
  parent_.resetDownstreamConnection();
}

// class ActiveMessageEncoderFilter
ActiveMessageEncoderFilter::ActiveMessageEncoderFilter(ActiveMessage& parent,
                                                       DubboFilters::EncoderFilterSharedPtr filter,
                                                       bool dual_filter)
    : ActiveMessageFilterBase(parent, dual_filter), handle_(filter) {}

void ActiveMessageEncoderFilter::continueEncoding() {
  ASSERT(parent_.context());
  auto state = ActiveMessage::FilterIterationStartState::AlwaysStartFromNext;
  if (0 != parent_.context()->originMessage().length()) {
    state = ActiveMessage::FilterIterationStartState::CanStartFromCurrent;
    ENVOY_LOG(warn, "The original message data is not consumed, triggering the encoder filter from "
                    "the current location");
  }
  const FilterStatus status = parent_.applyEncoderFilters(this, state);
  if (FilterStatus::Continue == status) {
    ENVOY_LOG(debug, "All encoding filters have been executed");
  }
}

// class ActiveMessage
ActiveMessage::ActiveMessage(ConnectionManager& parent)
    : parent_(parent), request_timer_(std::make_unique<Stats::HistogramCompletableTimespanImpl>(
                           parent_.stats().request_time_ms_, parent.timeSystem())),
      stream_id_(parent.randomGenerator().random()),
      stream_info_(parent.timeSystem(), parent_.connection().connectionInfoProviderSharedPtr(),
                   StreamInfo::FilterState::LifeSpan::FilterChain),
      pending_stream_decoded_(false), local_response_sent_(false) {
  parent_.stats().request_active_.inc();
}

ActiveMessage::~ActiveMessage() {
  parent_.stats().request_active_.dec();
  request_timer_->complete();
  for (auto& filter : decoder_filters_) {
    ENVOY_LOG(debug, "destroy decoder filter");
    filter->handler()->onDestroy();
  }

  for (auto& filter : encoder_filters_) {
    // Do not call on destroy twice for dual registered filters.
    if (!filter->dual_filter_) {
      ENVOY_LOG(debug, "destroy encoder filter");
      filter->handler()->onDestroy();
    }
  }
}

std::list<ActiveMessageEncoderFilterPtr>::iterator
ActiveMessage::commonEncodePrefix(ActiveMessageEncoderFilter* filter,
                                  FilterIterationStartState state) {
  // Only do base state setting on the initial call. Subsequent calls for filtering do not touch
  // the base state.
  if (filter == nullptr) {
    // ASSERT(!state_.local_complete_);
    // state_.local_complete_ = end_stream;
    return encoder_filters_.begin();
  }

  if (state == FilterIterationStartState::CanStartFromCurrent) {
    // The filter iteration has been stopped for all frame types, and now the iteration continues.
    // The current filter's encoding callback has not be called. Call it now.
    return filter->entry();
  }
  return std::next(filter->entry());
}

std::list<ActiveMessageDecoderFilterPtr>::iterator
ActiveMessage::commonDecodePrefix(ActiveMessageDecoderFilter* filter,
                                  FilterIterationStartState state) {
  if (!filter) {
    return decoder_filters_.begin();
  }
  if (state == FilterIterationStartState::CanStartFromCurrent) {
    // The filter iteration has been stopped for all frame types, and now the iteration continues.
    // The current filter's callback function has not been called. Call it now.
    return filter->entry();
  }
  return std::next(filter->entry());
}

void ActiveMessage::onStreamDecoded(MessageMetadataSharedPtr metadata, ContextSharedPtr ctx) {
  parent_.stats().request_decoding_success_.inc();

  metadata_ = metadata;
  context_ = ctx;
  filter_action_ = [metadata, ctx](DubboFilters::DecoderFilter* filter) -> FilterStatus {
    return filter->onMessageDecoded(metadata, ctx);
  };

  auto status = applyDecoderFilters(nullptr, FilterIterationStartState::CanStartFromCurrent);
  switch (status) {
  case FilterStatus::StopIteration:
    ENVOY_LOG(debug, "dubbo request: pause calling decoder filter, id is {}",
              metadata->requestId());
    pending_stream_decoded_ = true;
    return;
  case FilterStatus::AbortIteration:
    ENVOY_LOG(debug, "dubbo request: abort calling decoder filter, id is {}",
              metadata->requestId());
    parent_.deferredMessage(*this);
    return;
  case FilterStatus::Continue:
    ENVOY_LOG(debug, "dubbo request: complete processing of downstream request messages, id is {}",
              metadata->requestId());
    finalizeRequest();
    return;
  }
  PANIC_DUE_TO_CORRUPT_ENUM
}

void ActiveMessage::finalizeRequest() {
  pending_stream_decoded_ = false;
  parent_.stats().request_.inc();
  bool is_one_way = false;
  switch (metadata_->messageType()) {
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

FilterStatus ActiveMessage::applyDecoderFilters(ActiveMessageDecoderFilter* filter,
                                                FilterIterationStartState state) {
  ASSERT(filter_action_ != nullptr);
  if (!local_response_sent_) {
    for (auto entry = commonDecodePrefix(filter, state); entry != decoder_filters_.end(); entry++) {
      const FilterStatus status = filter_action_((*entry)->handler().get());
      if (local_response_sent_) {
        break;
      }

      if (status != FilterStatus::Continue) {
        return status;
      }
    }
  }

  filter_action_ = nullptr;

  return FilterStatus::Continue;
}

FilterStatus ActiveMessage::applyEncoderFilters(ActiveMessageEncoderFilter* filter,
                                                FilterIterationStartState state) {
  ASSERT(encoder_filter_action_ != nullptr);

  if (!local_response_sent_) {
    for (auto entry = commonEncodePrefix(filter, state); entry != encoder_filters_.end(); entry++) {
      const FilterStatus status = encoder_filter_action_((*entry)->handler().get());
      if (local_response_sent_) {
        break;
      }

      if (status != FilterStatus::Continue) {
        return status;
      }
    }
  }

  encoder_filter_action_ = nullptr;

  return FilterStatus::Continue;
}

void ActiveMessage::sendLocalReply(const DubboFilters::DirectResponse& response, bool end_stream) {
  ASSERT(metadata_);
  parent_.sendLocalReply(*metadata_, response, end_stream);

  if (end_stream) {
    return;
  }

  local_response_sent_ = true;
}

void ActiveMessage::startUpstreamResponse() {
  ENVOY_LOG(debug, "dubbo response: start upstream");

  ASSERT(response_decoder_ == nullptr);

  auto protocol =
      NamedProtocolConfigFactory::getFactory(protocolType()).createProtocol(serializationType());

  // Create a response message decoder.
  response_decoder_ = std::make_unique<ActiveResponseDecoder>(
      *this, parent_.stats(), parent_.connection(), std::move(protocol));
}

DubboFilters::UpstreamResponseStatus ActiveMessage::upstreamData(Buffer::Instance& buffer) {
  ASSERT(response_decoder_ != nullptr);

  TRY_NEEDS_AUDIT {
    auto status = response_decoder_->onData(buffer);
    if (status == DubboFilters::UpstreamResponseStatus::Complete) {
      if (requestId() != response_decoder_->requestId()) {
        throw EnvoyException(fmt::format("dubbo response: request ID is not equal, {}:{}",
                                         requestId(), response_decoder_->requestId()));
      }

      // Completed upstream response.
      parent_.deferredMessage(*this);
    } else if (status == DubboFilters::UpstreamResponseStatus::Retry) {
      response_decoder_.reset();
    }

    return status;
  }
  END_TRY catch (const DownstreamConnectionCloseException& ex) {
    ENVOY_CONN_LOG(error, "dubbo response: exception ({})", parent_.connection(), ex.what());
    onReset();
    parent_.stats().response_error_caused_connection_close_.inc();
    return DubboFilters::UpstreamResponseStatus::Reset;
  }
  catch (const EnvoyException& ex) {
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
  return metadata_ != nullptr ? metadata_->requestId() : 0;
}

uint64_t ActiveMessage::streamId() const { return stream_id_; }

SerializationType ActiveMessage::serializationType() const {
  return parent_.downstreamSerializationType();
}

ProtocolType ActiveMessage::protocolType() const { return parent_.downstreamProtocolType(); }

StreamInfo::StreamInfo& ActiveMessage::streamInfo() { return stream_info_; }

Event::Dispatcher& ActiveMessage::dispatcher() { return parent_.connection().dispatcher(); }

const Network::Connection* ActiveMessage::connection() const { return &parent_.connection(); }

void ActiveMessage::addDecoderFilter(DubboFilters::DecoderFilterSharedPtr filter) {
  addDecoderFilterWorker(filter, false);
}

void ActiveMessage::addEncoderFilter(DubboFilters::EncoderFilterSharedPtr filter) {
  addEncoderFilterWorker(filter, false);
}

void ActiveMessage::addFilter(DubboFilters::CodecFilterSharedPtr filter) {
  addDecoderFilterWorker(filter, true);
  addEncoderFilterWorker(filter, true);
}

void ActiveMessage::addDecoderFilterWorker(DubboFilters::DecoderFilterSharedPtr filter,
                                           bool dual_filter) {
  ActiveMessageDecoderFilterPtr wrapper =
      std::make_unique<ActiveMessageDecoderFilter>(*this, filter, dual_filter);
  filter->setDecoderFilterCallbacks(*wrapper);
  LinkedList::moveIntoListBack(std::move(wrapper), decoder_filters_);
}
void ActiveMessage::addEncoderFilterWorker(DubboFilters::EncoderFilterSharedPtr filter,
                                           bool dual_filter) {
  ActiveMessageEncoderFilterPtr wrapper =
      std::make_unique<ActiveMessageEncoderFilter>(*this, filter, dual_filter);
  filter->setEncoderFilterCallbacks(*wrapper);
  LinkedList::moveIntoListBack(std::move(wrapper), encoder_filters_);
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
