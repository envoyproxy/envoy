#include "extensions/filters/network/sip_proxy/conn_manager.h"

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"

#include "extensions/filters/network/sip_proxy/app_exception_impl.h"
#include "extensions/filters/network/sip_proxy/protocol.h"
#include "extensions/filters/network/sip_proxy/transport.h"
#include "extensions/filters/network/sip_proxy/encoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

ConnectionManager::ConnectionManager(Config& config, Random::RandomGenerator& random_generator,
                                     TimeSource& time_source,
                                     std::shared_ptr<Router::TransactionInfos> transaction_infos)
    : config_(config), stats_(config_.stats()), decoder_(std::make_unique<Decoder>(*this)),
      random_generator_(random_generator), time_source_(time_source),
      transaction_infos_(transaction_infos) {}

ConnectionManager::~ConnectionManager() = default;

Network::FilterStatus ConnectionManager::onData(Buffer::Instance& data, bool end_stream) {
  //ENVOY_LOG(trace, "ConnectionManager received data {}\n{}", data.length(), data.toString());
  request_buffer_.move(data);
  dispatch();

  if (end_stream) {
    ENVOY_CONN_LOG(info, "downstream half-closed", read_callbacks_->connection());

    //resetAllRpcs(false);
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }

  return Network::FilterStatus::StopIteration;
}

void ConnectionManager::dispatch() {
//  try {
    decoder_->onData(request_buffer_);
//    return;
//  } catch (const AppException& ex) {
//    ENVOY_LOG(error, "sip application exception: {}", ex.what());
//    if (transactions_.empty()) {
//      MessageMetadata metadata;
//      sendLocalReply(metadata, ex, true);
//    } else {
//      sendLocalReply(*(*rpcs_.begin())->metadata_, ex, true);
//    }
//  } catch (const EnvoyException& ex) {
//    ENVOY_CONN_LOG(error, "sip error: {}", read_callbacks_->connection(), ex.what());
//
//    if (rpcs_.empty()) {
//      // Just hang up since we don't know how to encode a response.
//      read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
//    } else {
//      // Use the current rpc's transport/protocol to send an error downstream.
//      rpcs_.front()->onError(ex.what());
//    }
//  }
//
//  stats_.request_decoding_error_.inc();
//  resetAllRpcs(true);
}

void ConnectionManager::sendLocalReply(MessageMetadata& metadata, const DirectResponse& response,
                                       bool end_stream) {
  if (read_callbacks_->connection().state() == Network::Connection::State::Closed) {
    return;
  }

  Buffer::OwnedImpl buffer;
  const DirectResponse::ResponseType result = response.encode(metadata, buffer);

  Buffer::OwnedImpl response_buffer;

  std::shared_ptr<Encoder> encoder = std::make_shared<EncoderImpl>();
  encoder->encode(std::make_shared<MessageMetadata>(metadata), response_buffer);

  read_callbacks_->connection().write(response_buffer, end_stream);
  if (end_stream) {
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }

  switch (result) {
  case DirectResponse::ResponseType::SuccessReply:
    stats_.response_success_.inc();
    break;
  case DirectResponse::ResponseType::ErrorReply:
    stats_.response_error_.inc();
    break;
  case DirectResponse::ResponseType::Exception:
    stats_.response_exception_.inc();
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

void ConnectionManager::continueDecoding() {
  ENVOY_CONN_LOG(info, "sip filter continued", read_callbacks_->connection());
  // dispatch();
  //  stopped_ = false;
  //  dispatch();
  //
  //  if (!stopped_ && half_closed_) {
  //    // If we're half closed, but not stopped waiting for an upstream, reset any pending rpcs and
  //    // close the connection.
  //    resetAllRpcs(false);
  //    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  //  }
}

void ConnectionManager::doDeferredRpcDestroy(ConnectionManager::ActiveTrans& trans) {
  transactions_.erase(trans.transactionId());
  //read_callbacks_->connection().dispatcher().deferredDelete(std::unique_ptr<Event::DeferredDeletable>(&trans));
}

void ConnectionManager::resetAllRpcs(bool local_reset) {
  ENVOY_LOG(info, "active_rpc to be deleted {}", transactions_.size());
  for (auto it = transactions_.cbegin(); it != transactions_.cend();) {
    if (local_reset) {
      ENVOY_CONN_LOG(debug, "local close with active request", read_callbacks_->connection());
      stats_.cx_destroy_local_with_active_rq_.inc();
    } else {
      ENVOY_CONN_LOG(debug, "remote close with active request", read_callbacks_->connection());
      stats_.cx_destroy_remote_with_active_rq_.inc();
    }

    it = transactions_.erase(it);
  }
}

void ConnectionManager::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;

  read_callbacks_->connection().addConnectionCallbacks(*this);
  read_callbacks_->connection().enableHalfClose(true);
}

void ConnectionManager::onEvent(Network::ConnectionEvent event) {
  ENVOY_CONN_LOG(info, "received event {}", read_callbacks_->connection(), event);
  resetAllRpcs(event == Network::ConnectionEvent::LocalClose);
}

DecoderEventHandler& ConnectionManager::newDecoderEventHandler(MessageMetadataSharedPtr metadata) {
  ENVOY_LOG(trace, "new decoder filter");
  std::string && k = std::string(metadata->transactionId().value());
  if (metadata->methodType() == MethodType::Ack) {
    if (transactions_.find(k) != transactions_.end()) {
      // ACK_4XX
      return *transactions_.at(k);
    }
  }

  ActiveTransPtr new_trans(new ActiveTrans(*this, metadata));
  new_trans->createFilterChain();
  transactions_.emplace(k, std::move(new_trans));

  return *transactions_.at(k);
}

bool ConnectionManager::ResponseDecoder::onData(MessageMetadataSharedPtr metadata) {
  metadata_ = metadata;
  if (auto status = transportBegin(metadata_); status == FilterStatus::StopIteration) {
    return true;
  }

  if (auto status = messageBegin(metadata_); status == FilterStatus::StopIteration) {
    return true;
  }

  if (auto status = messageEnd(); status == FilterStatus::StopIteration) {
    return true;
  }

  if (auto status = transportEnd(); status == FilterStatus::StopIteration) {
    return true;
  }

  return true;
}

FilterStatus ConnectionManager::ResponseDecoder::messageBegin(MessageMetadataSharedPtr metadata) {
  UNREFERENCED_PARAMETER(metadata);
  return FilterStatus::Continue;
}

FilterStatus ConnectionManager::ResponseDecoder::messageEnd() {
  if (first_reply_field_) {
    // When the response is sip void type there is never a fieldBegin call on a success
    // because the response struct has no fields and so the first field type is FieldType::Stop.
    // The decoder state machine handles FieldType::Stop by going immediately to structEnd,
    // skipping fieldBegin callback. Therefore if we are still waiting for the first reply field
    // at end of message then it is a void success.
    success_ = true;
    first_reply_field_ = false;
  }

  return FilterStatus::Continue;
}

FilterStatus ConnectionManager::ResponseDecoder::transportEnd() {
  ASSERT(metadata_ != nullptr);

  ConnectionManager& cm = parent_.parent_;

  if (cm.read_callbacks_->connection().state() == Network::Connection::State::Closed) {
    complete_ = true;
    throw EnvoyException("downstream connection is closed");
  }

  Buffer::OwnedImpl buffer;

  metadata_->setEP(cm.read_callbacks_->connection().addressProvider().localAddress()->ip()->addressAsString());
  // metadata_->setEP("127.0.0.1:5062");

  std::shared_ptr<Encoder> encoder = std::make_shared<EncoderImpl>();
  encoder->encode(metadata_, buffer);

  ENVOY_LOG(trace, "send response {}\n{}", buffer.length(), buffer.toString());
  cm.read_callbacks_->connection().write(buffer, false);

  cm.stats_.response_.inc();

  // switch (metadata_->messageType()) {
  // case MessageType::Reply:
  //   cm.stats_.response_reply_.inc();
  //   if (success_.value_or(false)) {
  //     cm.stats_.response_success_.inc();
  //   } else {
  //     cm.stats_.response_error_.inc();
  //   }
  //
  //   break;
  //
  // case MessageType::Exception:
  //   cm.stats_.response_exception_.inc();
  //   break;
  //
  // default:
  //   cm.stats_.response_invalid_type_.inc();
  //   break;
  // }

  return FilterStatus::Continue;
}

void ConnectionManager::ActiveTransDecoderFilter::continueDecoding() {
  const FilterStatus status = parent_.applyDecoderFilters(this);
  if (status == FilterStatus::Continue) {
    // All filters have been executed for the current decoder state.
    if (parent_.pending_transport_end_) {
      // If the filter stack was paused during transportEnd, handle end-of-request details.
      parent_.finalizeRequest();
    }

    parent_.continueDecoding();
  }
}

FilterStatus ConnectionManager::ActiveTrans::applyDecoderFilters(ActiveTransDecoderFilter* filter) {
  ASSERT(filter_action_ != nullptr);

  if (!local_response_sent_) {
    std::list<ActiveTransDecoderFilterPtr>::iterator entry;
    if (!filter) {
      entry = decoder_filters_.begin();
    } else {
      entry = std::next(filter->entry());
    }

    for (; entry != decoder_filters_.end(); entry++) {
      const FilterStatus status = filter_action_((*entry)->handle_.get());
      if (local_response_sent_) {
        // The filter called sendLocalReply: stop processing filters and return
        // FilterStatus::Continue irrespective of the current result.
        break;
      }

      if (status != FilterStatus::Continue) {
        return status;
      }
    }
  }

  filter_action_ = nullptr;
  filter_context_.reset();

  return FilterStatus::Continue;
}

FilterStatus ConnectionManager::ActiveTrans::transportBegin(MessageMetadataSharedPtr metadata) {
  filter_context_ = metadata;
  filter_action_ = [this](DecoderEventHandler* filter) -> FilterStatus {
    MessageMetadataSharedPtr metadata = absl::any_cast<MessageMetadataSharedPtr>(filter_context_);
    return filter->transportBegin(metadata);
  };

  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveTrans::transportEnd() {
  ASSERT(metadata_ != nullptr);

  FilterStatus status;
  filter_action_ = [](DecoderEventHandler* filter) -> FilterStatus {
    return filter->transportEnd();
  };

  status = applyDecoderFilters(nullptr);
  if (status == FilterStatus::StopIteration) {
    pending_transport_end_ = true;
    return status;
  }

  finalizeRequest();

  return status;
}

void ConnectionManager::ActiveTrans::finalizeRequest() {
  //  pending_transport_end_ = false;
  //
  //  parent_.stats_.request_.inc();
  //
  //  bool destroy_rpc = false;
  //  switch (original_msg_type_) {
  //  case MessageType::Call:
  //    parent_.stats_.request_call_.inc();
  //
  //    // Local response or protocol upgrade mean we don't wait for an upstream response.
  //    destroy_rpc = local_response_sent_ || (upgrade_handler_ != nullptr);
  //    break;
  //
  //  case MessageType::Oneway:
  //    parent_.stats_.request_oneway_.inc();
  //
  //    // No response forthcoming, we're done.
  //    destroy_rpc = true;
  //    break;
  //
  //  default:
  //    parent_.stats_.request_invalid_type_.inc();
  //
  //    // Invalid request, implies no response.
  //    destroy_rpc = true;
  //    break;
  //  }
  //
  //  if (destroy_rpc) {
  //    parent_.doDeferredRpcDestroy(*this);
  //  }
}

FilterStatus ConnectionManager::ActiveTrans::messageBegin(MessageMetadataSharedPtr metadata) {
  metadata_ = metadata;
  filter_context_ = metadata;
  filter_action_ = [this](DecoderEventHandler* filter) -> FilterStatus {
    MessageMetadataSharedPtr metadata = absl::any_cast<MessageMetadataSharedPtr>(filter_context_);
    return filter->messageBegin(metadata);
  };

  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveTrans::messageEnd() {
  filter_action_ = [](DecoderEventHandler* filter) -> FilterStatus { return filter->messageEnd(); };
  return applyDecoderFilters(nullptr);
}

void ConnectionManager::ActiveTrans::createFilterChain() {
  parent_.config_.filterFactory().createFilterChain(*this);
}

void ConnectionManager::ActiveTrans::onReset() {
  // TODO(zuercher): e.g., parent_.stats_.named_.downstream_rq_rx_reset_.inc();
  parent_.doDeferredRpcDestroy(*this);
}

void ConnectionManager::ActiveTrans::onError(const std::string& what) {
  if (metadata_) {
    sendLocalReply(AppException(AppExceptionType::ProtocolError, what), true);
    return;
  }

  // Transport or protocol error happened before (or during message begin) parsing. It's not
  // possible to provide a valid response, so don't try.

  parent_.doDeferredRpcDestroy(*this);
  parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

const Network::Connection* ConnectionManager::ActiveTrans::connection() const {
  return &parent_.read_callbacks_->connection();
}

Router::RouteConstSharedPtr ConnectionManager::ActiveTrans::route() {
  if (!cached_route_) {
    if (metadata_ != nullptr) {
      Router::RouteConstSharedPtr route =
          parent_.config_.routerConfig().route(*metadata_, stream_id_);
      cached_route_ = std::move(route);
    } else {
      cached_route_ = nullptr;
    }
  }

  return cached_route_.value();
}

void ConnectionManager::ActiveTrans::sendLocalReply(const DirectResponse& response,
                                                    bool end_stream) {
  parent_.sendLocalReply(*metadata_, response, end_stream);

  if (end_stream) {
    return;
  }

  // Consume any remaining request data from the downstream.
  local_response_sent_ = true;
}

void ConnectionManager::ActiveTrans::startUpstreamResponse() {
  // ASSERT(response_decoder_ == nullptr);

  response_decoder_ = std::make_unique<ResponseDecoder>(*this);
}

SipFilters::ResponseStatus
ConnectionManager::ActiveTrans::upstreamData(MessageMetadataSharedPtr metadata) {
  ASSERT(response_decoder_ != nullptr);

  try {
    if (response_decoder_->onData(metadata)) {
      // Completed upstream response.
      // parent_.doDeferredRpcDestroy(*this);
      return SipFilters::ResponseStatus::Complete;
    }
    return SipFilters::ResponseStatus::MoreData;
  } catch (const AppException& ex) {
    ENVOY_LOG(error, "sip response application error: {}", ex.what());
    parent_.stats_.response_decoding_error_.inc();

    sendLocalReply(ex, true);
    return SipFilters::ResponseStatus::Reset;
  } catch (const EnvoyException& ex) {
    ENVOY_CONN_LOG(error, "sip response error: {}", parent_.read_callbacks_->connection(),
                   ex.what());
    parent_.stats_.response_decoding_error_.inc();

    onError(ex.what());
    return SipFilters::ResponseStatus::Reset;
  }
}

void ConnectionManager::ActiveTrans::resetDownstreamConnection() {
  parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
