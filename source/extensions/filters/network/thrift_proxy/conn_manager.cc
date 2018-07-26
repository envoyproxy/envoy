#include "extensions/filters/network/thrift_proxy/conn_manager.h"

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"

#include "extensions/filters/network/thrift_proxy/app_exception_impl.h"
#include "extensions/filters/network/thrift_proxy/binary_protocol_impl.h"
#include "extensions/filters/network/thrift_proxy/compact_protocol_impl.h"
#include "extensions/filters/network/thrift_proxy/framed_transport_impl.h"
#include "extensions/filters/network/thrift_proxy/unframed_transport_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

ConnectionManager::ConnectionManager(Config& config)
    : config_(config), stats_(config_.stats()), decoder_(config_.createDecoder(*this)) {}

ConnectionManager::~ConnectionManager() {}

Network::FilterStatus ConnectionManager::onData(Buffer::Instance& data, bool end_stream) {
  UNREFERENCED_PARAMETER(end_stream);

  request_buffer_.move(data);
  dispatch();

  return Network::FilterStatus::StopIteration;
}

void ConnectionManager::dispatch() {
  if (stopped_) {
    ENVOY_LOG(error, "thrift filter stopped");
    return;
  }

  try {
    bool underflow = false;
    while (!underflow) {
      ThriftFilters::FilterStatus status = decoder_->onData(request_buffer_, underflow);
      if (status == ThriftFilters::FilterStatus::StopIteration) {
        stopped_ = true;
        break;
      }
    }
  } catch (const EnvoyException& ex) {
    ENVOY_LOG(error, "thrift error: {}", ex.what());
    stats_.request_decoding_error_.inc();

    // Use the current rpc to send an error downstream, if possible.
    rpcs_.front()->onError(ex.what());

    resetAllRpcs();
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }
}

void ConnectionManager::continueDecoding() {
  stopped_ = false;
  dispatch();
}

void ConnectionManager::doDeferredRpcDestroy(ConnectionManager::ActiveRpc& rpc) {
  read_callbacks_->connection().dispatcher().deferredDelete(rpc.removeFromList(rpcs_));
}

void ConnectionManager::resetAllRpcs() {
  while (!rpcs_.empty()) {
    rpcs_.front()->onReset();
  }
}

void ConnectionManager::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
}

void ConnectionManager::onEvent(Network::ConnectionEvent event) {
  if (!rpcs_.empty()) {
    if (event == Network::ConnectionEvent::RemoteClose) {
      stats_.cx_destroy_remote_with_active_rq_.inc();
    } else if (event == Network::ConnectionEvent::LocalClose) {
      stats_.cx_destroy_local_with_active_rq_.inc();
    }

    resetAllRpcs();
  }
}

ThriftFilters::DecoderFilter& ConnectionManager::newDecoderFilter() {
  ENVOY_LOG(debug, "new decoder filter");

  ActiveRpcPtr new_rpc(new ActiveRpc(*this));
  new_rpc->createFilterChain();
  new_rpc->moveIntoList(std::move(new_rpc), rpcs_);

  return **rpcs_.begin();
}

bool ConnectionManager::ResponseDecoder::onData(Buffer::Instance& data) {
  upstream_buffer_.move(data);

  bool underflow = false;
  decoder_->onData(upstream_buffer_, underflow);
  ASSERT(complete_ || underflow);
  return complete_;
}

ThriftFilters::FilterStatus ConnectionManager::ResponseDecoder::messageBegin(absl::string_view name,
                                                                             MessageType msg_type,
                                                                             int32_t seq_id) {
  reply_.emplace(std::string(name), msg_type, seq_id);
  first_reply_field_ = (msg_type == MessageType::Reply);
  return ProtocolConverter::messageBegin(name, msg_type, seq_id);
}

ThriftFilters::FilterStatus ConnectionManager::ResponseDecoder::fieldBegin(absl::string_view name,
                                                                           FieldType field_type,
                                                                           int16_t field_id) {
  if (first_reply_field_) {
    // Reply messages contain a struct where field 0 is the call result and fields 1+ are
    // exceptions, if defined. At most one field may be set. Therefore, the very first field we
    // encounter in a reply is either field 0 (success) or not (IDL exception returned).
    ASSERT(reply_.has_value());
    reply_.value().success_ = field_id == 0 && field_type != FieldType::Stop;
    first_reply_field_ = false;
  }

  return ProtocolConverter::fieldBegin(name, field_type, field_id);
}

ThriftFilters::FilterStatus ConnectionManager::ResponseDecoder::transportEnd() {
  ConnectionManager& cm = parent_.parent_;

  Buffer::OwnedImpl buffer;

  // Use the factory to get the concrete transport from the decoder transport (as opposed to
  // potentially pre-detection auto transport).
  TransportPtr transport =
      NamedTransportConfigFactory::getFactory(parent_.parent_.decoder_->transportType())
          .createTransport();
  transport->encodeFrame(buffer, parent_.response_buffer_);
  complete_ = true;

  cm.read_callbacks_->connection().write(buffer, false);

  cm.stats_.response_.inc();

  ASSERT(reply_.has_value());
  switch (reply_.value().msg_type_) {
  case MessageType::Reply:
    cm.stats_.response_reply_.inc();
    if (reply_.value().success_.value_or(false)) {
      cm.stats_.response_success_.inc();
    } else {
      cm.stats_.response_error_.inc();
    }

    break;

  case MessageType::Exception:
    cm.stats_.response_exception_.inc();
    break;

  default:
    cm.stats_.response_invalid_type_.inc();
    break;
  }

  return ThriftFilters::FilterStatus::Continue;
}

ThriftFilters::FilterStatus ConnectionManager::ActiveRpc::transportEnd() {
  ASSERT(call_.has_value());

  parent_.stats_.request_.inc();

  switch (call_.value().msg_type_) {
  case MessageType::Call:
    parent_.stats_.request_call_.inc();
    break;

  case MessageType::Oneway:
    parent_.stats_.request_oneway_.inc();

    // No response forthcoming, we're done.
    parent_.doDeferredRpcDestroy(*this);
    break;

  default:
    parent_.stats_.request_invalid_type_.inc();
    break;
  }

  return decoder_filter_->transportEnd();
}

void ConnectionManager::ActiveRpc::createFilterChain() {
  parent_.config_.filterFactory().createFilterChain(*this);
}

void ConnectionManager::ActiveRpc::onReset() {
  // TODO(zuercher): e.g., parent_.stats_.named_.downstream_rq_rx_reset_.inc();
  parent_.doDeferredRpcDestroy(*this);
}

void ConnectionManager::ActiveRpc::onError(const std::string& what) {
  if (call_.has_value()) {
    const Message& msg = call_.value();
    sendLocalReply(std::make_unique<AppException>(msg.method_name_, msg.seq_id_,
                                                  AppExceptionType::ProtocolError, what));
    return;
  }

  // Transport or protocol error happened before (or during message begin) parsing. It's not
  // possible to provide a valid response, so don't try.
}

const Network::Connection* ConnectionManager::ActiveRpc::connection() const {
  return &parent_.read_callbacks_->connection();
}

void ConnectionManager::ActiveRpc::continueDecoding() { parent_.continueDecoding(); }

Router::RouteConstSharedPtr ConnectionManager::ActiveRpc::route() {
  if (!cached_route_) {
    if (call_.has_value()) {
      Router::RouteConstSharedPtr route =
          parent_.config_.routerConfig().route(call_.value().method_name_);
      cached_route_ = std::move(route);
    } else {
      cached_route_ = nullptr;
    }
  }

  return cached_route_.value();
}

void ConnectionManager::ActiveRpc::sendLocalReply(ThriftFilters::DirectResponsePtr&& response) {
  // Use the factory to get the concrete protocol from the decoder protocol (as opposed to
  // potentially pre-detection auto protocol).
  ProtocolPtr proto =
      NamedProtocolConfigFactory::getFactory(parent_.decoder_->protocolType()).createProtocol();
  Buffer::OwnedImpl buffer;

  response->encode(*proto, buffer);

  // Same logic as protocol above.
  TransportPtr transport =
      NamedTransportConfigFactory::getFactory(parent_.decoder_->transportType()).createTransport();
  transport->encodeFrame(response_buffer_, buffer);

  parent_.read_callbacks_->connection().write(response_buffer_, false);
  parent_.doDeferredRpcDestroy(*this);
}

void ConnectionManager::ActiveRpc::startUpstreamResponse(TransportType transport_type,
                                                         ProtocolType protocol_type) {
  ASSERT(response_decoder_ == nullptr);

  response_decoder_ = std::make_unique<ResponseDecoder>(*this, transport_type, protocol_type);
}

bool ConnectionManager::ActiveRpc::upstreamData(Buffer::Instance& buffer) {
  ASSERT(response_decoder_ != nullptr);

  try {
    bool complete = response_decoder_->onData(buffer);
    if (complete) {
      parent_.doDeferredRpcDestroy(*this);
    }
    return complete;
  } catch (const EnvoyException& ex) {
    ENVOY_LOG(error, "thrift response error: {}", ex.what());
    parent_.stats_.response_decoding_error_.inc();

    onError(ex.what());
    decoder_filter_->resetUpstreamConnection();
    return true;
  }
}

void ConnectionManager::ActiveRpc::resetDownstreamConnection() {
  parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
  parent_.doDeferredRpcDestroy(*this);
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
