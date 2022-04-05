#include "source/extensions/filters/network/thrift_proxy/conn_manager.h"

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"

#include "source/extensions/filters/network/thrift_proxy/app_exception_impl.h"
#include "source/extensions/filters/network/thrift_proxy/protocol.h"
#include "source/extensions/filters/network/thrift_proxy/transport.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

ConnectionManager::ConnectionManager(Config& config, Random::RandomGenerator& random_generator,
                                     TimeSource& time_source,
                                     const Network::DrainDecision& drain_decision)
    : config_(config), stats_(config_.stats()), transport_(config.createTransport()),
      protocol_(config.createProtocol()),
      decoder_(std::make_unique<Decoder>(*transport_, *protocol_, *this)),
      random_generator_(random_generator), time_source_(time_source),
      drain_decision_(drain_decision) {}

ConnectionManager::~ConnectionManager() = default;

Network::FilterStatus ConnectionManager::onData(Buffer::Instance& data, bool end_stream) {
  request_buffer_.move(data);
  dispatch();

  if (end_stream) {
    ENVOY_CONN_LOG(trace, "downstream half-closed", read_callbacks_->connection());

    // Downstream has closed. Unless we're waiting for an upstream connection to complete a oneway
    // request, close. The special case for oneway requests allows them to complete before the
    // ConnectionManager is destroyed.
    if (stopped_) {
      ASSERT(!rpcs_.empty());
      MessageMetadata& metadata = *(*rpcs_.begin())->metadata_;
      ASSERT(metadata.hasMessageType());
      if (metadata.messageType() == MessageType::Oneway) {
        ENVOY_CONN_LOG(trace, "waiting for one-way completion", read_callbacks_->connection());
        half_closed_ = true;
        return Network::FilterStatus::StopIteration;
      }
    }

    resetAllRpcs(false);
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }

  return Network::FilterStatus::StopIteration;
}

void ConnectionManager::dispatch() {
  if (stopped_) {
    ENVOY_CONN_LOG(debug, "thrift filter stopped", read_callbacks_->connection());
    return;
  }

  if (requests_overflow_) {
    ENVOY_CONN_LOG(debug, "thrift filter requests overflow", read_callbacks_->connection());
    return;
  }

  try {
    bool underflow = false;
    while (!underflow) {
      FilterStatus status = decoder_->onData(request_buffer_, underflow);
      if (status == FilterStatus::StopIteration) {
        stopped_ = true;
        break;
      }
    }

    return;
  } catch (const AppException& ex) {
    ENVOY_LOG(debug, "thrift application exception: {}", ex.what());
    if (rpcs_.empty()) {
      MessageMetadata metadata;
      sendLocalReply(metadata, ex, true);
    } else {
      sendLocalReply(*(*rpcs_.begin())->metadata_, ex, true);
    }
  } catch (const EnvoyException& ex) {
    ENVOY_CONN_LOG(debug, "thrift error: {}", read_callbacks_->connection(), ex.what());

    if (rpcs_.empty()) {
      // Transport/protocol mismatch (including errors in automatic detection). Just hang up
      // since we don't know how to encode a response.
      read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    } else {
      // Use the current rpc's transport/protocol to send an error downstream.
      rpcs_.front()->onError(ex.what());
    }
  }

  stats_.request_decoding_error_.inc();
  resetAllRpcs(true);
}

void ConnectionManager::sendLocalReply(MessageMetadata& metadata, const DirectResponse& response,
                                       bool end_stream) {
  if (read_callbacks_->connection().state() == Network::Connection::State::Closed) {
    return;
  }

  Buffer::OwnedImpl buffer;
  const DirectResponse::ResponseType result = response.encode(metadata, *protocol_, buffer);

  Buffer::OwnedImpl response_buffer;
  metadata.setProtocol(protocol_->type());
  transport_->encodeFrame(response_buffer, metadata, buffer);

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
  }
}

void ConnectionManager::continueDecoding() {
  ENVOY_CONN_LOG(debug, "thrift filter continued", read_callbacks_->connection());
  stopped_ = false;
  dispatch();

  if (!stopped_ && half_closed_) {
    // If we're half closed, but not stopped waiting for an upstream, reset any pending rpcs and
    // close the connection.
    resetAllRpcs(false);
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }
}

void ConnectionManager::doDeferredRpcDestroy(ConnectionManager::ActiveRpc& rpc) {
  read_callbacks_->connection().dispatcher().deferredDelete(rpc.removeFromList(rpcs_));
  if (requests_overflow_ && rpcs_.empty()) {
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }
}

void ConnectionManager::resetAllRpcs(bool local_reset) {
  while (!rpcs_.empty()) {
    if (local_reset) {
      ENVOY_CONN_LOG(debug, "local close with active request", read_callbacks_->connection());
      stats_.cx_destroy_local_with_active_rq_.inc();
    } else {
      ENVOY_CONN_LOG(debug, "remote close with active request", read_callbacks_->connection());
      stats_.cx_destroy_remote_with_active_rq_.inc();
    }

    rpcs_.front()->onReset();
  }
}

void ConnectionManager::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;

  read_callbacks_->connection().addConnectionCallbacks(*this);
  read_callbacks_->connection().enableHalfClose(true);
}

void ConnectionManager::onEvent(Network::ConnectionEvent event) {
  if (event != Network::ConnectionEvent::LocalClose &&
      event != Network::ConnectionEvent::RemoteClose) {
    return;
  }
  resetAllRpcs(event == Network::ConnectionEvent::LocalClose);
}

DecoderEventHandler& ConnectionManager::newDecoderEventHandler() {
  ENVOY_LOG(trace, "new decoder filter");

  ActiveRpcPtr new_rpc(new ActiveRpc(*this));
  new_rpc->createFilterChain();
  LinkedList::moveIntoList(std::move(new_rpc), rpcs_);

  return **rpcs_.begin();
}

bool ConnectionManager::passthroughEnabled() const {
  if (!config_.payloadPassthrough()) {
    return false;
  }

  // If the rpcs list is empty, a local response happened.
  //
  // TODO(rgs1): we actually could still enable passthrough for local
  // responses as long as the transport is framed and the protocol is
  // not Twitter.
  if (rpcs_.empty()) {
    return false;
  }

  return (*rpcs_.begin())->passthroughSupported();
}

bool ConnectionManager::ResponseDecoder::onData(Buffer::Instance& data) {
  upstream_buffer_.move(data);

  bool underflow = false;
  decoder_->onData(upstream_buffer_, underflow);
  ASSERT(complete_ || underflow);
  return complete_;
}

FilterStatus ConnectionManager::ResponseDecoder::passthroughData(Buffer::Instance& data) {
  passthrough_ = true;
  return ProtocolConverter::passthroughData(data);
}

FilterStatus ConnectionManager::ResponseDecoder::messageBegin(MessageMetadataSharedPtr metadata) {
  metadata_ = metadata;
  metadata_->setSequenceId(parent_.original_sequence_id_);

  if (metadata->hasReplyType()) {
    success_ = metadata->replyType() == ReplyType::Success;
  }

  // Check if the upstream host is draining.
  //
  // Note: the drain header needs to be checked here in messageBegin, and not transportBegin, so
  // that we can support the header in TTwitter protocol, which reads/adds response headers to
  // metadata in messageBegin when reading the response from upstream. Therefore detecting a drain
  // should happen here.
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.thrift_connection_draining")) {
    metadata_->setDraining(!metadata->headers().get(Headers::get().Drain).empty());
    metadata->headers().remove(Headers::get().Drain);

    // Check if this host itself is draining.
    //
    // Note: Similarly as above, the response is buffered until transportEnd. Therefore metadata
    // should be set before the encodeFrame() call. It should be set at or after the messageBegin
    // call so that the header is added after all upstream headers passed, due to messageBegin
    // possibly not getting headers in transportBegin.
    ConnectionManager& cm = parent_.parent_;
    if (cm.drain_decision_.drainClose()) {
      // TODO(rgs1): should the key value contain something useful (e.g.: minutes til drain is
      // over)?
      metadata->headers().addReferenceKey(Headers::get().Drain, "true");
      parent_.parent_.stats_.downstream_response_drain_close_.inc();
    }
  }

  return ProtocolConverter::messageBegin(metadata);
}

FilterStatus ConnectionManager::ResponseDecoder::transportEnd() {
  ASSERT(metadata_ != nullptr);

  ConnectionManager& cm = parent_.parent_;

  if (cm.read_callbacks_->connection().state() == Network::Connection::State::Closed) {
    complete_ = true;
    throw EnvoyException("downstream connection is closed");
  }

  Buffer::OwnedImpl buffer;

  // Use the factory to get the concrete transport from the decoder transport (as opposed to
  // potentially pre-detection auto transport).
  TransportPtr transport =
      NamedTransportConfigFactory::getFactory(parent_.parent_.decoder_->transportType())
          .createTransport();

  metadata_->setProtocol(parent_.parent_.decoder_->protocolType());
  transport->encodeFrame(buffer, *metadata_, parent_.response_buffer_);
  complete_ = true;

  cm.read_callbacks_->connection().write(buffer, false);

  cm.stats_.response_.inc();
  if (passthrough_) {
    cm.stats_.response_passthrough_.inc();
  }

  switch (metadata_->messageType()) {
  case MessageType::Reply:
    cm.stats_.response_reply_.inc();
    if (success_) {
      if (success_.value()) {
        cm.stats_.response_success_.inc();
      } else {
        cm.stats_.response_error_.inc();
      }
    }

    break;

  case MessageType::Exception:
    cm.stats_.response_exception_.inc();
    break;

  default:
    cm.stats_.response_invalid_type_.inc();
    break;
  }

  return FilterStatus::Continue;
}

bool ConnectionManager::ResponseDecoder::passthroughEnabled() const {
  return parent_.parent_.passthroughEnabled();
}

void ConnectionManager::ActiveRpcDecoderFilter::continueDecoding() {
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

FilterStatus ConnectionManager::ActiveRpc::applyDecoderFilters(ActiveRpcDecoderFilter* filter) {
  ASSERT(filter_action_ != nullptr);

  if (local_response_sent_) {
    filter_action_ = nullptr;
    filter_context_.reset();
    return FilterStatus::Continue;
  }

  if (upgrade_handler_) {
    // Divert events to the current protocol upgrade handler.
    const FilterStatus status = filter_action_(upgrade_handler_.get());
    filter_context_.reset();
    return status;
  }

  std::list<ActiveRpcDecoderFilterPtr>::iterator entry =
      !filter ? decoder_filters_.begin() : std::next(filter->entry());
  for (; entry != decoder_filters_.end(); entry++) {
    const FilterStatus status = filter_action_((*entry)->handle_.get());
    if (local_response_sent_) {
      // The filter called sendLocalReply but _did not_ close the connection.
      // We return FilterStatus::Continue irrespective of the current result,
      // which is fine because subsequent calls to this method will skip
      // filters anyway.
      //
      // Note: we need to return FilterStatus::Continue here, in order for decoding
      // to proceed. This is important because as noted above, the connection remains
      // open so we need to consume the remaining bytes.
      break;
    }

    if (status != FilterStatus::Continue) {
      // If we got FilterStatus::StopIteration and a local reply happened but
      // local_response_sent_ was not set, the connection was closed.
      //
      // In this case, either resetAllRpcs() gets called via onEvent(LocalClose) or
      // dispatch() stops the processing.
      //
      // In other words, after a local reply closes the connection and StopIteration
      // is returned we are done.
      return status;
    }
  }

  filter_action_ = nullptr;
  filter_context_.reset();

  return FilterStatus::Continue;
}

FilterStatus ConnectionManager::ActiveRpc::transportBegin(MessageMetadataSharedPtr metadata) {
  filter_context_ = metadata;
  filter_action_ = [this](DecoderEventHandler* filter) -> FilterStatus {
    MessageMetadataSharedPtr metadata = absl::any_cast<MessageMetadataSharedPtr>(filter_context_);
    return filter->transportBegin(metadata);
  };

  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveRpc::transportEnd() {
  ASSERT(metadata_ != nullptr);

  FilterStatus status;
  if (upgrade_handler_) {
    status = upgrade_handler_->transportEnd();

    if (metadata_->isProtocolUpgradeMessage()) {
      ENVOY_CONN_LOG(error, "thrift: sending protocol upgrade response",
                     parent_.read_callbacks_->connection());
      sendLocalReply(*parent_.protocol_->upgradeResponse(*upgrade_handler_), false);
    }
  } else {
    filter_action_ = [](DecoderEventHandler* filter) -> FilterStatus {
      return filter->transportEnd();
    };

    status = applyDecoderFilters(nullptr);
    if (status == FilterStatus::StopIteration) {
      pending_transport_end_ = true;
      return status;
    }
  }

  finalizeRequest();

  return status;
}

void ConnectionManager::ActiveRpc::finalizeRequest() {
  pending_transport_end_ = false;

  parent_.stats_.request_.inc();

  parent_.accumulated_requests_++;
  if (parent_.config_.maxRequestsPerConnection() > 0 &&
      parent_.accumulated_requests_ >= parent_.config_.maxRequestsPerConnection()) {
    parent_.read_callbacks_->connection().readDisable(true);
    parent_.requests_overflow_ = true;
    parent_.stats_.downstream_cx_max_requests_.inc();
  }

  if (passthrough_) {
    parent_.stats_.request_passthrough_.inc();
  }

  bool destroy_rpc = false;
  switch (original_msg_type_) {
  case MessageType::Call:
    parent_.stats_.request_call_.inc();

    // Local response or protocol upgrade mean we don't wait for an upstream response.
    destroy_rpc = local_response_sent_ || (upgrade_handler_ != nullptr);
    break;

  case MessageType::Oneway:
    parent_.stats_.request_oneway_.inc();

    // No response forthcoming, we're done.
    destroy_rpc = true;
    break;

  default:
    parent_.stats_.request_invalid_type_.inc();

    // Invalid request, implies no response.
    destroy_rpc = true;
    break;
  }

  if (destroy_rpc) {
    parent_.doDeferredRpcDestroy(*this);
  }
}

bool ConnectionManager::ActiveRpc::passthroughSupported() const {
  for (auto& entry : decoder_filters_) {
    if (!entry->handle_->passthroughSupported()) {
      return false;
    }
  }
  return true;
}

FilterStatus ConnectionManager::ActiveRpc::passthroughData(Buffer::Instance& data) {
  passthrough_ = true;
  filter_context_ = &data;
  filter_action_ = [this](DecoderEventHandler* filter) -> FilterStatus {
    Buffer::Instance* data = absl::any_cast<Buffer::Instance*>(filter_context_);
    return filter->passthroughData(*data);
  };

  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveRpc::messageBegin(MessageMetadataSharedPtr metadata) {
  ASSERT(metadata->hasSequenceId());
  ASSERT(metadata->hasMessageType());

  metadata_ = metadata;
  original_sequence_id_ = metadata_->sequenceId();
  original_msg_type_ = metadata_->messageType();

  if (metadata_->isProtocolUpgradeMessage()) {
    ASSERT(parent_.protocol_->supportsUpgrade());

    ENVOY_CONN_LOG(debug, "thrift: decoding protocol upgrade request",
                   parent_.read_callbacks_->connection());
    upgrade_handler_ = parent_.protocol_->upgradeRequestDecoder();
    ASSERT(upgrade_handler_ != nullptr);
  }

  filter_context_ = metadata;
  filter_action_ = [this](DecoderEventHandler* filter) -> FilterStatus {
    MessageMetadataSharedPtr metadata = absl::any_cast<MessageMetadataSharedPtr>(filter_context_);
    return filter->messageBegin(metadata);
  };

  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveRpc::messageEnd() {
  filter_action_ = [](DecoderEventHandler* filter) -> FilterStatus { return filter->messageEnd(); };
  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveRpc::structBegin(absl::string_view name) {
  filter_context_ = std::string(name);
  filter_action_ = [this](DecoderEventHandler* filter) -> FilterStatus {
    std::string& name = absl::any_cast<std::string&>(filter_context_);
    return filter->structBegin(name);
  };

  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveRpc::structEnd() {
  filter_action_ = [](DecoderEventHandler* filter) -> FilterStatus { return filter->structEnd(); };
  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveRpc::fieldBegin(absl::string_view name, FieldType& field_type,
                                                      int16_t& field_id) {
  filter_context_ =
      std::tuple<std::string, FieldType, int16_t>(std::string(name), field_type, field_id);
  filter_action_ = [this](DecoderEventHandler* filter) -> FilterStatus {
    std::tuple<std::string, FieldType, int16_t>& t =
        absl::any_cast<std::tuple<std::string, FieldType, int16_t>&>(filter_context_);
    std::string& name = std::get<0>(t);
    FieldType& field_type = std::get<1>(t);
    int16_t& field_id = std::get<2>(t);
    return filter->fieldBegin(name, field_type, field_id);
  };

  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveRpc::fieldEnd() {
  filter_action_ = [](DecoderEventHandler* filter) -> FilterStatus { return filter->fieldEnd(); };
  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveRpc::boolValue(bool& value) {
  filter_context_ = value;
  filter_action_ = [this](DecoderEventHandler* filter) -> FilterStatus {
    bool& value = absl::any_cast<bool&>(filter_context_);
    return filter->boolValue(value);
  };

  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveRpc::byteValue(uint8_t& value) {
  filter_context_ = value;
  filter_action_ = [this](DecoderEventHandler* filter) -> FilterStatus {
    uint8_t& value = absl::any_cast<uint8_t&>(filter_context_);
    return filter->byteValue(value);
  };

  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveRpc::int16Value(int16_t& value) {
  filter_context_ = value;
  filter_action_ = [this](DecoderEventHandler* filter) -> FilterStatus {
    int16_t& value = absl::any_cast<int16_t&>(filter_context_);
    return filter->int16Value(value);
  };

  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveRpc::int32Value(int32_t& value) {
  filter_context_ = value;
  filter_action_ = [this](DecoderEventHandler* filter) -> FilterStatus {
    int32_t& value = absl::any_cast<int32_t&>(filter_context_);
    return filter->int32Value(value);
  };

  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveRpc::int64Value(int64_t& value) {
  filter_context_ = value;
  filter_action_ = [this](DecoderEventHandler* filter) -> FilterStatus {
    int64_t& value = absl::any_cast<int64_t&>(filter_context_);
    return filter->int64Value(value);
  };

  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveRpc::doubleValue(double& value) {
  filter_context_ = value;
  filter_action_ = [this](DecoderEventHandler* filter) -> FilterStatus {
    double& value = absl::any_cast<double&>(filter_context_);
    return filter->doubleValue(value);
  };

  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveRpc::stringValue(absl::string_view value) {
  filter_context_ = std::string(value);

  filter_action_ = [this](DecoderEventHandler* filter) -> FilterStatus {
    std::string& value = absl::any_cast<std::string&>(filter_context_);
    return filter->stringValue(value);
  };

  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveRpc::mapBegin(FieldType& key_type, FieldType& value_type,
                                                    uint32_t& size) {
  filter_context_ = std::tuple<FieldType, FieldType, uint32_t>(key_type, value_type, size);

  filter_action_ = [this](DecoderEventHandler* filter) -> FilterStatus {
    std::tuple<FieldType, FieldType, uint32_t>& t =
        absl::any_cast<std::tuple<FieldType, FieldType, uint32_t>&>(filter_context_);
    FieldType& key_type = std::get<0>(t);
    FieldType& value_type = std::get<1>(t);
    uint32_t& size = std::get<2>(t);
    return filter->mapBegin(key_type, value_type, size);
  };

  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveRpc::mapEnd() {
  filter_action_ = [](DecoderEventHandler* filter) -> FilterStatus { return filter->mapEnd(); };
  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveRpc::listBegin(FieldType& value_type, uint32_t& size) {
  filter_context_ = std::tuple<FieldType, uint32_t>(value_type, size);

  filter_action_ = [this](DecoderEventHandler* filter) -> FilterStatus {
    std::tuple<FieldType, uint32_t>& t =
        absl::any_cast<std::tuple<FieldType, uint32_t>&>(filter_context_);
    FieldType& value_type = std::get<0>(t);
    uint32_t& size = std::get<1>(t);
    return filter->listBegin(value_type, size);
  };

  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveRpc::listEnd() {
  filter_action_ = [](DecoderEventHandler* filter) -> FilterStatus { return filter->listEnd(); };
  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveRpc::setBegin(FieldType& value_type, uint32_t& size) {
  filter_context_ = std::tuple<FieldType, uint32_t>(value_type, size);

  filter_action_ = [this](DecoderEventHandler* filter) -> FilterStatus {
    std::tuple<FieldType, uint32_t>& t =
        absl::any_cast<std::tuple<FieldType, uint32_t>&>(filter_context_);
    FieldType& value_type = std::get<0>(t);
    uint32_t& size = std::get<1>(t);
    return filter->setBegin(value_type, size);
  };

  return applyDecoderFilters(nullptr);
}

FilterStatus ConnectionManager::ActiveRpc::setEnd() {
  filter_action_ = [](DecoderEventHandler* filter) -> FilterStatus { return filter->setEnd(); };
  return applyDecoderFilters(nullptr);
}

void ConnectionManager::ActiveRpc::createFilterChain() {
  parent_.config_.filterFactory().createFilterChain(*this);
}

void ConnectionManager::ActiveRpc::onReset() {
  // TODO(zuercher): e.g., parent_.stats_.named_.downstream_rq_rx_reset_.inc();
  parent_.doDeferredRpcDestroy(*this);
}

void ConnectionManager::ActiveRpc::onError(const std::string& what) {
  if (metadata_) {
    sendLocalReply(AppException(AppExceptionType::ProtocolError, what), true);
    return;
  }

  // Transport or protocol error happened before (or during message begin) parsing. It's not
  // possible to provide a valid response, so don't try.

  parent_.doDeferredRpcDestroy(*this);
  parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

const Network::Connection* ConnectionManager::ActiveRpc::connection() const {
  return &parent_.read_callbacks_->connection();
}

Router::RouteConstSharedPtr ConnectionManager::ActiveRpc::route() {
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

void ConnectionManager::ActiveRpc::sendLocalReply(const DirectResponse& response, bool end_stream) {
  metadata_->setSequenceId(original_sequence_id_);
  parent_.sendLocalReply(*metadata_, response, end_stream);

  if (end_stream) {
    return;
  }

  if (!upgrade_handler_) {
    // Consume any remaining request data from the downstream.
    local_response_sent_ = true;
  }
}

void ConnectionManager::ActiveRpc::startUpstreamResponse(Transport& transport, Protocol& protocol) {
  ASSERT(response_decoder_ == nullptr);

  response_decoder_ = std::make_unique<ResponseDecoder>(*this, transport, protocol);
}

ThriftFilters::ResponseStatus ConnectionManager::ActiveRpc::upstreamData(Buffer::Instance& buffer) {
  ASSERT(response_decoder_ != nullptr);

  try {
    if (response_decoder_->onData(buffer)) {
      // Completed upstream response.
      parent_.doDeferredRpcDestroy(*this);
      return ThriftFilters::ResponseStatus::Complete;
    }
    return ThriftFilters::ResponseStatus::MoreData;
  } catch (const AppException& ex) {
    ENVOY_LOG(error, "thrift response application error: {}", ex.what());
    parent_.stats_.response_decoding_error_.inc();

    sendLocalReply(ex, true);
    return ThriftFilters::ResponseStatus::Reset;
  } catch (const EnvoyException& ex) {
    ENVOY_CONN_LOG(error, "thrift response error: {}", parent_.read_callbacks_->connection(),
                   ex.what());
    parent_.stats_.response_decoding_error_.inc();

    onError(ex.what());
    return ThriftFilters::ResponseStatus::Reset;
  }
}

void ConnectionManager::ActiveRpc::resetDownstreamConnection() {
  parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
