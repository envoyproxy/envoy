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

ConnectionManager::ConnectionManager(const ConfigSharedPtr& config,
                                     Random::RandomGenerator& random_generator,
                                     TimeSource& time_source,
                                     const Network::DrainDecision& drain_decision)
    : config_(config), stats_(config_->stats()), transport_(config_->createTransport()),
      protocol_(config_->createProtocol()),
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

void ConnectionManager::emitLogEntry(const Http::RequestHeaderMap* request_headers,
                                     const Http::ResponseHeaderMap* response_headers,
                                     const StreamInfo::StreamInfo& stream_info) {
  const Formatter::HttpFormatterContext log_context{request_headers, response_headers};

  for (const auto& access_log : config_->accessLogs()) {
    access_log->log(log_context, stream_info);
  }
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

  TRY_NEEDS_AUDIT {
    bool underflow = false;
    while (!underflow) {
      FilterStatus status = decoder_->onData(request_buffer_, underflow);
      if (status == FilterStatus::StopIteration) {
        stopped_ = true;
        break;
      }
    }

    return;
  }
  END_TRY catch (const AppException& ex) {
    ENVOY_LOG(debug, "thrift application exception: {}", ex.what());
    if (rpcs_.empty()) {
      MessageMetadata metadata;
      sendLocalReply(metadata, ex, true);
    } else {
      sendLocalReply(*(*rpcs_.begin())->metadata_, ex, true);
    }
  }
  catch (const EnvoyException& ex) {
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

absl::optional<DirectResponse::ResponseType>
ConnectionManager::sendLocalReply(MessageMetadata& metadata, const DirectResponse& response,
                                  bool end_stream) {
  if (read_callbacks_->connection().state() == Network::Connection::State::Closed) {
    return absl::nullopt;
  }

  DirectResponse::ResponseType result = DirectResponse::ResponseType::Exception;

  if (!metadata.hasMessageType() || metadata.messageType() != MessageType::Oneway) {
    Buffer::OwnedImpl buffer;
    result = response.encode(metadata, *protocol_, buffer);
    Buffer::OwnedImpl response_buffer;
    metadata.setProtocol(protocol_->type());
    transport_->encodeFrame(response_buffer, metadata, buffer);

    read_callbacks_->connection().write(response_buffer, end_stream);
  }

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
  return result;
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
  if (!rpc.inserted()) {
    return;
  }

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

bool ConnectionManager::ResponseDecoder::passthroughEnabled() const {
  return parent_.parent_.passthroughEnabled();
}

bool ConnectionManager::passthroughEnabled() const {
  if (!config_->payloadPassthrough()) {
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

bool ConnectionManager::headerKeysPreserveCase() const { return config_->headerKeysPreserveCase(); }

bool ConnectionManager::ResponseDecoder::onData(Buffer::Instance& data) {
  upstream_buffer_.move(data);

  bool underflow = false;
  decoder_->onData(upstream_buffer_, underflow);
  ASSERT(complete_ || underflow);
  return complete_;
}

FilterStatus ConnectionManager::ResponseDecoder::transportBegin(MessageMetadataSharedPtr metadata) {
  return parent_.applyEncoderFilters(DecoderEvent::TransportBegin, metadata, protocol_converter_);
}

FilterStatus ConnectionManager::ResponseDecoder::transportEnd() {
  ASSERT(metadata_ != nullptr);

  FilterStatus status = parent_.applyEncoderFilters(DecoderEvent::TransportEnd, absl::monostate(),
                                                    protocol_converter_);
  // Currently we don't support returning FilterStatus::StopIteration from encoder filters.
  // Hence, this if-statement is always false.
  ASSERT(status == FilterStatus::Continue);
  if (status == FilterStatus::StopIteration) {
    pending_transport_end_ = true;
    return FilterStatus::StopIteration;
  }

  finalizeResponse();
  return FilterStatus::Continue;
}

void ConnectionManager::ResponseDecoder::finalizeResponse() {
  pending_transport_end_ = false;
  ConnectionManager& cm = parent_.parent_;

  if (cm.read_callbacks_->connection().state() == Network::Connection::State::Closed) {
    complete_ = true;
    throw EnvoyException("downstream connection is closed");
  }

  Buffer::OwnedImpl buffer;

  // Use the factory to get the concrete transport from the decoder transport (as opposed to
  // potentially pre-detection auto transport).
  TransportPtr transport =
      NamedTransportConfigFactory::getFactory(cm.decoder_->transportType()).createTransport();

  metadata_->setProtocol(cm.decoder_->protocolType());
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
}

FilterStatus ConnectionManager::ResponseDecoder::passthroughData(Buffer::Instance& data) {
  passthrough_ = true;

  return parent_.applyEncoderFilters(DecoderEvent::PassthroughData, &data, protocol_converter_);
}

FilterStatus ConnectionManager::ResponseDecoder::messageBegin(MessageMetadataSharedPtr metadata) {
  metadata_ = metadata;
  metadata_->setSequenceId(parent_.original_sequence_id_);

  if (metadata->hasReplyType()) {
    // TODO(kuochunghsu): the status of success could be altered by filters
    success_ = metadata->replyType() == ReplyType::Success;
  }

  ConnectionManager& cm = parent_.parent_;

  ENVOY_STREAM_LOG(
      trace, "Response message_type: {}, seq_id: {}, method: {}, frame size: {}, headers:\n{}",
      parent_,
      metadata->hasMessageType() ? MessageTypeNames::get().fromType(metadata->messageType()) : "-",
      metadata_->sequenceId(), metadata->hasMethodName() ? metadata->methodName() : "-",
      metadata->hasFrameSize() ? metadata->frameSize() : -1, metadata->responseHeaders());

  // Check if the upstream host is draining.
  //
  // Note: the drain header needs to be checked here in messageBegin, and not transportBegin, so
  // that we can support the header in TTwitter protocol, which reads/adds response headers to
  // metadata in messageBegin when reading the response from upstream. Therefore detecting a drain
  // should happen here.
  metadata_->setDraining(!metadata->responseHeaders().get(Headers::get().Drain).empty());
  metadata->responseHeaders().remove(Headers::get().Drain);

  // Check if this host itself is draining.
  //
  // Note: Similarly as above, the response is buffered until transportEnd. Therefore metadata
  // should be set before the encodeFrame() call. It should be set at or after the messageBegin
  // call so that the header is added after all upstream headers passed, due to messageBegin
  // possibly not getting headers in transportBegin.
  if (cm.drain_decision_.drainClose()) {
    ENVOY_STREAM_LOG(debug, "propogate Drain header for drain close decision", parent_);
    // TODO(rgs1): should the key value contain something useful (e.g.: minutes til drain is
    // over)?
    metadata->responseHeaders().addReferenceKey(Headers::get().Drain, "true");
    cm.stats_.downstream_response_drain_close_.inc();
  }

  parent_.recordResponseAccessLog(metadata);

  return parent_.applyEncoderFilters(DecoderEvent::MessageBegin, metadata, protocol_converter_);
}

FilterStatus ConnectionManager::ResponseDecoder::messageEnd() {
  return parent_.applyEncoderFilters(DecoderEvent::MessageEnd, absl::monostate(),
                                     protocol_converter_);
}

FilterStatus ConnectionManager::ResponseDecoder::structBegin(absl::string_view name) {
  return parent_.applyEncoderFilters(DecoderEvent::StructBegin, std::string(name),
                                     protocol_converter_);
}

FilterStatus ConnectionManager::ResponseDecoder::structEnd() {
  return parent_.applyEncoderFilters(DecoderEvent::StructEnd, absl::monostate(),
                                     protocol_converter_);
}

FilterStatus ConnectionManager::ResponseDecoder::fieldBegin(absl::string_view name,
                                                            FieldType& field_type,
                                                            int16_t& field_id) {
  return parent_.applyEncoderFilters(DecoderEvent::FieldBegin,
                                     std::make_tuple(std::string(name), field_type, field_id),
                                     protocol_converter_);
}

FilterStatus ConnectionManager::ResponseDecoder::fieldEnd() {
  return parent_.applyEncoderFilters(DecoderEvent::FieldEnd, absl::monostate(),
                                     protocol_converter_);
}

FilterStatus ConnectionManager::ResponseDecoder::boolValue(bool& value) {
  return parent_.applyEncoderFilters(DecoderEvent::BoolValue, value, protocol_converter_);
}

FilterStatus ConnectionManager::ResponseDecoder::byteValue(uint8_t& value) {
  return parent_.applyEncoderFilters(DecoderEvent::ByteValue, value, protocol_converter_);
}

FilterStatus ConnectionManager::ResponseDecoder::int16Value(int16_t& value) {
  return parent_.applyEncoderFilters(DecoderEvent::Int16Value, value, protocol_converter_);
}

FilterStatus ConnectionManager::ResponseDecoder::int32Value(int32_t& value) {
  return parent_.applyEncoderFilters(DecoderEvent::Int32Value, value, protocol_converter_);
}

FilterStatus ConnectionManager::ResponseDecoder::int64Value(int64_t& value) {
  return parent_.applyEncoderFilters(DecoderEvent::Int64Value, value, protocol_converter_);
}

FilterStatus ConnectionManager::ResponseDecoder::doubleValue(double& value) {
  return parent_.applyEncoderFilters(DecoderEvent::DoubleValue, value, protocol_converter_);
}

FilterStatus ConnectionManager::ResponseDecoder::stringValue(absl::string_view value) {
  return parent_.applyEncoderFilters(DecoderEvent::StringValue, std::string(value),
                                     protocol_converter_);
}

FilterStatus ConnectionManager::ResponseDecoder::mapBegin(FieldType& key_type,
                                                          FieldType& value_type, uint32_t& size) {
  return parent_.applyEncoderFilters(
      DecoderEvent::MapBegin, std::make_tuple(key_type, value_type, size), protocol_converter_);
}

FilterStatus ConnectionManager::ResponseDecoder::mapEnd() {
  return parent_.applyEncoderFilters(DecoderEvent::MapEnd, absl::monostate(), protocol_converter_);
}

FilterStatus ConnectionManager::ResponseDecoder::listBegin(FieldType& elem_type, uint32_t& size) {
  return parent_.applyEncoderFilters(DecoderEvent::ListBegin, std::make_tuple(elem_type, size),
                                     protocol_converter_);
}

FilterStatus ConnectionManager::ResponseDecoder::listEnd() {
  return parent_.applyEncoderFilters(DecoderEvent::ListEnd, absl::monostate(), protocol_converter_);
}

FilterStatus ConnectionManager::ResponseDecoder::setBegin(FieldType& elem_type, uint32_t& size) {
  return parent_.applyEncoderFilters(DecoderEvent::SetBegin, std::make_tuple(elem_type, size),
                                     protocol_converter_);
}

FilterStatus ConnectionManager::ResponseDecoder::setEnd() {
  return parent_.applyEncoderFilters(DecoderEvent::SetEnd, absl::monostate(), protocol_converter_);
}

bool ConnectionManager::ResponseDecoder::headerKeysPreserveCase() const {
  return parent_.parent_.headerKeysPreserveCase();
}

void ConnectionManager::ActiveRpcDecoderFilter::continueDecoding() {
  const FilterStatus status =
      parent_.applyDecoderFilters(DecoderEvent::ContinueDecode, absl::monostate(), this);
  if (status == FilterStatus::Continue) {
    // All filters have been executed for the current decoder state.
    if (parent_.pending_transport_end_) {
      // If the filter stack was paused during transportEnd, handle end-of-request details.
      parent_.finalizeRequest();
    }

    parent_.continueDecoding();
  }
}

void ConnectionManager::ActiveRpcEncoderFilter::continueEncoding() {
  // Not supported.
  ASSERT(false);
}

FilterStatus ConnectionManager::ActiveRpc::applyDecoderFilters(DecoderEvent state,
                                                               FilterContext&& data,
                                                               ActiveRpcDecoderFilter* filter) {
  ASSERT(filter_action_ == nullptr || state == DecoderEvent::ContinueDecode);
  prepareFilterAction(state, std::move(data));

  if (local_response_sent_) {
    filter_action_ = nullptr;
    return FilterStatus::Continue;
  }

  if (upgrade_handler_) {
    // Divert events to the current protocol upgrade handler.
    const FilterStatus status = filter_action_(upgrade_handler_.get());
    filter_action_ = nullptr;
    return status;
  }

  return applyFilters<ActiveRpcDecoderFilter>(filter, decoder_filters_);
}

FilterStatus
ConnectionManager::ActiveRpc::applyEncoderFilters(DecoderEvent state, FilterContext&& data,
                                                  ProtocolConverterSharedPtr protocol_converter,
                                                  ActiveRpcEncoderFilter* filter) {
  ASSERT(filter_action_ == nullptr || state == DecoderEvent::ContinueDecode);
  prepareFilterAction(state, std::move(data));

  FilterStatus status =
      applyFilters<ActiveRpcEncoderFilter>(filter, encoder_filters_, protocol_converter);
  // FilterStatus::StopIteration is currently not supported.
  ASSERT(status == FilterStatus::Continue);

  return status;
}

template <typename FilterType>
FilterStatus
ConnectionManager::ActiveRpc::applyFilters(FilterType* filter,
                                           std::list<std::unique_ptr<FilterType>>& filter_list,
                                           ProtocolConverterSharedPtr protocol_converter) {

  typename std::list<std::unique_ptr<FilterType>>::iterator entry =
      !filter ? filter_list.begin() : std::next(filter->entry());
  for (; entry != filter_list.end(); entry++) {
    const FilterStatus status = filter_action_((*entry)->decodeEventHandler());
    ENVOY_STREAM_LOG(trace, "apply filter called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));
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

  // The protocol converter writes the data to a buffer for response.
  if (protocol_converter) {
    filter_action_(protocol_converter.get());
  }

  filter_action_ = nullptr;

  return FilterStatus::Continue;
}

void ConnectionManager::ActiveRpc::prepareFilterAction(DecoderEvent event, FilterContext&& data) {
  // DecoderEvent::ContinueDecode indicates we're handling previous filter action with the
  // filter chain. Therefore, we should not reset filter_action_.
  if (event == DecoderEvent::ContinueDecode) {
    return;
  }

  switch (event) {
  case DecoderEvent::TransportBegin:
    filter_action_ = [filter_context =
                          std::move(data)](DecoderEventHandler* filter) -> FilterStatus {
      MessageMetadataSharedPtr metadata = absl::get<MessageMetadataSharedPtr>(filter_context);
      return filter->transportBegin(metadata);
    };
    break;
  case DecoderEvent::TransportEnd:
    filter_action_ = [](DecoderEventHandler* filter) -> FilterStatus {
      return filter->transportEnd();
    };
    break;
  case DecoderEvent::PassthroughData:
    filter_action_ = [filter_context =
                          std::move(data)](DecoderEventHandler* filter) -> FilterStatus {
      Buffer::Instance* data = absl::get<Buffer::Instance*>(filter_context);
      return filter->passthroughData(*data);
    };
    break;
  case DecoderEvent::MessageBegin:
    filter_action_ = [filter_context =
                          std::move(data)](DecoderEventHandler* filter) -> FilterStatus {
      MessageMetadataSharedPtr metadata = absl::get<MessageMetadataSharedPtr>(filter_context);
      return filter->messageBegin(metadata);
    };
    break;
  case DecoderEvent::MessageEnd:
    filter_action_ = [](DecoderEventHandler* filter) -> FilterStatus {
      return filter->messageEnd();
    };
    break;
  case DecoderEvent::StructBegin:
    filter_action_ = [filter_context =
                          std::move(data)](DecoderEventHandler* filter) mutable -> FilterStatus {
      std::string& name = absl::get<std::string>(filter_context);
      return filter->structBegin(name);
    };
    break;
  case DecoderEvent::StructEnd:
    filter_action_ = [](DecoderEventHandler* filter) -> FilterStatus {
      return filter->structEnd();
    };
    break;
  case DecoderEvent::FieldBegin:
    filter_action_ = [filter_context =
                          std::move(data)](DecoderEventHandler* filter) mutable -> FilterStatus {
      std::tuple<std::string, FieldType, int16_t>& t =
          absl::get<std::tuple<std::string, FieldType, int16_t>>(filter_context);
      std::string& name = std::get<0>(t);
      FieldType& field_type = std::get<1>(t);
      int16_t& field_id = std::get<2>(t);
      return filter->fieldBegin(name, field_type, field_id);
    };
    break;
  case DecoderEvent::FieldEnd:
    filter_action_ = [](DecoderEventHandler* filter) -> FilterStatus { return filter->fieldEnd(); };
    break;
  case DecoderEvent::BoolValue:
    filter_action_ = [filter_context =
                          std::move(data)](DecoderEventHandler* filter) mutable -> FilterStatus {
      bool& value = absl::get<bool>(filter_context);
      return filter->boolValue(value);
    };
    break;
  case DecoderEvent::ByteValue:
    filter_action_ = [filter_context =
                          std::move(data)](DecoderEventHandler* filter) mutable -> FilterStatus {
      uint8_t& value = absl::get<uint8_t>(filter_context);
      return filter->byteValue(value);
    };
    break;
  case DecoderEvent::Int16Value:
    filter_action_ = [filter_context =
                          std::move(data)](DecoderEventHandler* filter) mutable -> FilterStatus {
      int16_t& value = absl::get<int16_t>(filter_context);
      return filter->int16Value(value);
    };
    break;
  case DecoderEvent::Int32Value:
    filter_action_ = [filter_context =
                          std::move(data)](DecoderEventHandler* filter) mutable -> FilterStatus {
      int32_t& value = absl::get<int32_t>(filter_context);
      return filter->int32Value(value);
    };
    break;
  case DecoderEvent::Int64Value:
    filter_action_ = [filter_context =
                          std::move(data)](DecoderEventHandler* filter) mutable -> FilterStatus {
      int64_t& value = absl::get<int64_t>(filter_context);
      return filter->int64Value(value);
    };
    break;
  case DecoderEvent::DoubleValue:
    filter_action_ = [filter_context =
                          std::move(data)](DecoderEventHandler* filter) mutable -> FilterStatus {
      double& value = absl::get<double>(filter_context);
      return filter->doubleValue(value);
    };
    break;
  case DecoderEvent::StringValue:
    filter_action_ = [filter_context =
                          std::move(data)](DecoderEventHandler* filter) mutable -> FilterStatus {
      std::string& value = absl::get<std::string>(filter_context);
      return filter->stringValue(value);
    };
    break;
  case DecoderEvent::MapBegin:
    filter_action_ = [filter_context =
                          std::move(data)](DecoderEventHandler* filter) mutable -> FilterStatus {
      std::tuple<FieldType, FieldType, uint32_t>& t =
          absl::get<std::tuple<FieldType, FieldType, uint32_t>>(filter_context);
      FieldType& key_type = std::get<0>(t);
      FieldType& value_type = std::get<1>(t);
      uint32_t& size = std::get<2>(t);
      return filter->mapBegin(key_type, value_type, size);
    };
    break;
  case DecoderEvent::MapEnd:
    filter_action_ = [](DecoderEventHandler* filter) -> FilterStatus { return filter->mapEnd(); };
    break;
  case DecoderEvent::ListBegin:
    filter_action_ = [filter_context =
                          std::move(data)](DecoderEventHandler* filter) mutable -> FilterStatus {
      std::tuple<FieldType, uint32_t>& t =
          absl::get<std::tuple<FieldType, uint32_t>>(filter_context);
      FieldType& elem_type = std::get<0>(t);
      uint32_t& size = std::get<1>(t);
      return filter->listBegin(elem_type, size);
    };
    break;
  case DecoderEvent::ListEnd:
    filter_action_ = [](DecoderEventHandler* filter) -> FilterStatus { return filter->listEnd(); };
    break;
  case DecoderEvent::SetBegin:
    filter_action_ = [filter_context =
                          std::move(data)](DecoderEventHandler* filter) mutable -> FilterStatus {
      std::tuple<FieldType, uint32_t>& t =
          absl::get<std::tuple<FieldType, uint32_t>>(filter_context);
      FieldType& elem_type = std::get<0>(t);
      uint32_t& size = std::get<1>(t);
      return filter->setBegin(elem_type, size);
    };
    break;
  case DecoderEvent::SetEnd:
    filter_action_ = [](DecoderEventHandler* filter) -> FilterStatus { return filter->setEnd(); };
    break;
  default:
    PANIC_DUE_TO_CORRUPT_ENUM;
  }
}

FilterStatus ConnectionManager::ActiveRpc::transportBegin(MessageMetadataSharedPtr metadata) {
  return applyDecoderFilters(DecoderEvent::TransportBegin, metadata);
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
    status = applyDecoderFilters(DecoderEvent::TransportEnd, absl::monostate());
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
  if (parent_.config_->maxRequestsPerConnection() > 0 &&
      parent_.accumulated_requests_ >= parent_.config_->maxRequestsPerConnection()) {
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

// TODO(kuochunghsu): passthroughSupported for decoder/encoder filters with more flexibility.
// That is, supporting passthrough data for decoder filters if all  decoder filters agree,
// and supporting passthrough data for encoder filters if all encoder filters agree.
bool ConnectionManager::ActiveRpc::passthroughSupported() const {
  for (auto& entry : decoder_filters_) {
    if (!entry->decoder_handle_->passthroughSupported()) {
      return false;
    }
  }
  for (auto& entry : encoder_filters_) {
    if (!entry->encoder_handle_->passthroughSupported()) {
      return false;
    }
  }
  return true;
}

void ConnectionManager::ActiveRpc::recordResponseAccessLog(
    DirectResponse::ResponseType direct_response_type, const MessageMetadataSharedPtr& metadata) {
  if (direct_response_type == DirectResponse::ResponseType::Exception) {
    recordResponseAccessLog(MessageTypeNames::get().fromType(MessageType::Exception), "-");
    return;
  }
  const std::string& message_type_name =
      metadata->hasMessageType() ? MessageTypeNames::get().fromType(metadata->messageType()) : "-";
  const std::string& reply_type_name = ReplyTypeNames::get().fromType(
      direct_response_type == DirectResponse::ResponseType::SuccessReply ? ReplyType::Success
                                                                         : ReplyType::Error);
  recordResponseAccessLog(message_type_name, reply_type_name);
}

void ConnectionManager::ActiveRpc::recordResponseAccessLog(
    const MessageMetadataSharedPtr& metadata) {
  recordResponseAccessLog(
      metadata->hasMessageType() ? MessageTypeNames::get().fromType(metadata->messageType()) : "-",
      metadata->hasReplyType() ? ReplyTypeNames::get().fromType(metadata->replyType()) : "-");
}

void ConnectionManager::ActiveRpc::recordResponseAccessLog(const std::string& message_type,
                                                           const std::string& reply_type) {
  ProtobufWkt::Struct stats_obj;
  auto& fields_map = *stats_obj.mutable_fields();
  auto& response_fields_map = *fields_map["response"].mutable_struct_value()->mutable_fields();

  response_fields_map["transport_type"] =
      ValueUtil::stringValue(TransportNames::get().fromType(downstreamTransportType()));
  response_fields_map["protocol_type"] =
      ValueUtil::stringValue(ProtocolNames::get().fromType(downstreamProtocolType()));
  response_fields_map["message_type"] = ValueUtil::stringValue(message_type);
  response_fields_map["reply_type"] = ValueUtil::stringValue(reply_type);

  streamInfo().setDynamicMetadata("thrift.proxy", stats_obj);
}

FilterStatus ConnectionManager::ActiveRpc::passthroughData(Buffer::Instance& data) {
  passthrough_ = true;
  return applyDecoderFilters(DecoderEvent::PassthroughData, &data);
}

FilterStatus ConnectionManager::ActiveRpc::messageBegin(MessageMetadataSharedPtr metadata) {
  ASSERT(metadata->hasSequenceId());
  ASSERT(metadata->hasMessageType());

  metadata_ = metadata;
  original_sequence_id_ = metadata_->sequenceId();
  original_msg_type_ = metadata_->messageType();

  auto& connection = parent_.read_callbacks_->connection();

  if (metadata_->isProtocolUpgradeMessage()) {
    ASSERT(parent_.protocol_->supportsUpgrade());

    ENVOY_CONN_LOG(debug, "thrift: decoding protocol upgrade request", connection);
    upgrade_handler_ = parent_.protocol_->upgradeRequestDecoder();
    ASSERT(upgrade_handler_ != nullptr);
  }

  FilterStatus result = FilterStatus::StopIteration;
  absl::optional<std::string> error;
  TRY_NEEDS_AUDIT { result = applyDecoderFilters(DecoderEvent::MessageBegin, metadata); }
  END_TRY catch (const std::bad_function_call& e) { error = std::string(e.what()); }

  const auto& route_ptr = route();
  const std::string& cluster_name = route_ptr ? route_ptr->routeEntry()->clusterName() : "-";
  const std::string& method = metadata->hasMethodName() ? metadata->methodName() : "-";
  const int32_t frame_size =
      metadata->hasFrameSize() ? static_cast<int32_t>(metadata->frameSize()) : -1;

  if (error.has_value()) {
    parent_.stats_.request_internal_error_.inc();
    std::ostringstream oss;
    parent_.read_callbacks_->connection().dumpState(oss, 0);
    ENVOY_STREAM_LOG(error,
                     "Catch exception: {}. Request seq_id: {}, method: {}, frame size: {}, cluster "
                     "name: {}, downstream connection state {}, headers:\n{}",
                     *this, error.value(), metadata_->sequenceId(), method, frame_size,
                     cluster_name, oss.str(), metadata->requestHeaders());
    throw EnvoyException(error.value());
  }

  ProtobufWkt::Struct stats_obj;
  auto& fields_map = *stats_obj.mutable_fields();
  fields_map["cluster"] = ValueUtil::stringValue(cluster_name);
  fields_map["method"] = ValueUtil::stringValue(method);
  fields_map["passthrough"] = ValueUtil::stringValue(passthroughSupported() ? "true" : "false");

  auto& request_fields_map = *fields_map["request"].mutable_struct_value()->mutable_fields();
  request_fields_map["transport_type"] =
      ValueUtil::stringValue(TransportNames::get().fromType(downstreamTransportType()));
  request_fields_map["protocol_type"] =
      ValueUtil::stringValue(ProtocolNames::get().fromType(downstreamProtocolType()));
  request_fields_map["message_type"] = ValueUtil::stringValue(
      metadata->hasMessageType() ? MessageTypeNames::get().fromType(metadata->messageType()) : "-");

  streamInfo().setDynamicMetadata("thrift.proxy", stats_obj);
  ENVOY_STREAM_LOG(trace, "Request seq_id: {}, method: {}, frame size: {}, headers:\n{}", *this,
                   metadata_->sequenceId(), method, frame_size, metadata->requestHeaders());

  return result;
}

FilterStatus ConnectionManager::ActiveRpc::messageEnd() {
  return applyDecoderFilters(DecoderEvent::MessageEnd, absl::monostate());
}

FilterStatus ConnectionManager::ActiveRpc::structBegin(absl::string_view name) {
  return applyDecoderFilters(DecoderEvent::StructBegin, std::string(name));
}

FilterStatus ConnectionManager::ActiveRpc::structEnd() {
  return applyDecoderFilters(DecoderEvent::StructEnd, absl::monostate());
}

FilterStatus ConnectionManager::ActiveRpc::fieldBegin(absl::string_view name, FieldType& field_type,
                                                      int16_t& field_id) {
  return applyDecoderFilters(DecoderEvent::FieldBegin,
                             std::make_tuple(std::string(name), field_type, field_id));
}

FilterStatus ConnectionManager::ActiveRpc::fieldEnd() {
  return applyDecoderFilters(DecoderEvent::FieldEnd, absl::monostate());
}

FilterStatus ConnectionManager::ActiveRpc::boolValue(bool& value) {
  return applyDecoderFilters(DecoderEvent::BoolValue, value);
}

FilterStatus ConnectionManager::ActiveRpc::byteValue(uint8_t& value) {
  return applyDecoderFilters(DecoderEvent::ByteValue, value);
}

FilterStatus ConnectionManager::ActiveRpc::int16Value(int16_t& value) {
  return applyDecoderFilters(DecoderEvent::Int16Value, value);
}

FilterStatus ConnectionManager::ActiveRpc::int32Value(int32_t& value) {
  return applyDecoderFilters(DecoderEvent::Int32Value, value);
}

FilterStatus ConnectionManager::ActiveRpc::int64Value(int64_t& value) {
  return applyDecoderFilters(DecoderEvent::Int64Value, value);
}

FilterStatus ConnectionManager::ActiveRpc::doubleValue(double& value) {
  return applyDecoderFilters(DecoderEvent::DoubleValue, value);
}

FilterStatus ConnectionManager::ActiveRpc::stringValue(absl::string_view value) {
  return applyDecoderFilters(DecoderEvent::StringValue, std::string(value));
}

FilterStatus ConnectionManager::ActiveRpc::mapBegin(FieldType& key_type, FieldType& value_type,
                                                    uint32_t& size) {
  return applyDecoderFilters(DecoderEvent::MapBegin, std::make_tuple(key_type, value_type, size));
}

FilterStatus ConnectionManager::ActiveRpc::mapEnd() {
  return applyDecoderFilters(DecoderEvent::MapEnd, absl::monostate());
}

FilterStatus ConnectionManager::ActiveRpc::listBegin(FieldType& value_type, uint32_t& size) {
  return applyDecoderFilters(DecoderEvent::ListBegin, std::make_tuple(value_type, size));
}

FilterStatus ConnectionManager::ActiveRpc::listEnd() {
  return applyDecoderFilters(DecoderEvent::ListEnd, absl::monostate());
}

FilterStatus ConnectionManager::ActiveRpc::setBegin(FieldType& value_type, uint32_t& size) {
  return applyDecoderFilters(DecoderEvent::SetBegin, std::make_tuple(value_type, size));
}

FilterStatus ConnectionManager::ActiveRpc::setEnd() {
  return applyDecoderFilters(DecoderEvent::SetEnd, absl::monostate());
}

void ConnectionManager::ActiveRpc::createFilterChain() {
  parent_.config_->filterFactory().createFilterChain(*this);
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
          parent_.config_->routerConfig().route(*metadata_, stream_id_);
      cached_route_ = std::move(route);
    } else {
      cached_route_ = absl::nullopt;
    }
  }

  return cached_route_.value();
}

void ConnectionManager::ActiveRpc::onLocalReply(const MessageMetadata& metadata, bool end_stream) {
  under_on_local_reply_ = true;
  for (auto& filter : base_filters_) {
    filter->onLocalReply(metadata, end_stream);
  }
  under_on_local_reply_ = false;
}

void ConnectionManager::ActiveRpc::sendLocalReply(const DirectResponse& response, bool end_stream) {
  ASSERT(!under_on_local_reply_);
  ENVOY_STREAM_LOG(debug, "Sending local reply, end_stream: {}, seq_id: {}", *this, end_stream,
                   original_sequence_id_);
  localReplyMetadata_ = metadata_->createResponseMetadata();
  localReplyMetadata_->setSequenceId(original_sequence_id_);

  onLocalReply(*localReplyMetadata_, end_stream);

  if (end_stream) {
    localReplyMetadata_->responseHeaders().addReferenceKey(Headers::get().Drain, "true");
    ConnectionManager& cm = parent_;
    cm.stats_.downstream_response_drain_close_.inc();
  }

  auto result = parent_.sendLocalReply(*localReplyMetadata_, response, end_stream);
  // Only report while the local reply is successfully written.
  if (result.has_value()) {
    recordResponseAccessLog(*result, localReplyMetadata_);
  }

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

  TRY_NEEDS_AUDIT {
    if (response_decoder_->onData(buffer)) {
      // Completed upstream response.
      parent_.doDeferredRpcDestroy(*this);
      return ThriftFilters::ResponseStatus::Complete;
    }
    return ThriftFilters::ResponseStatus::MoreData;
  }
  END_TRY catch (const AppException& ex) {
    ENVOY_LOG(error, "thrift response application error: {}", ex.what());
    parent_.stats_.response_decoding_error_.inc();

    sendLocalReply(ex, true);
    return ThriftFilters::ResponseStatus::Reset;
  }
  catch (const EnvoyException& ex) {
    ENVOY_CONN_LOG(error, "thrift response error: {}", parent_.read_callbacks_->connection(),
                   ex.what());
    parent_.stats_.response_decoding_error_.inc();

    onError(ex.what());
    return ThriftFilters::ResponseStatus::Reset;
  }
}

void ConnectionManager::ActiveRpc::clearRouteCache() { cached_route_ = absl::nullopt; }

void ConnectionManager::ActiveRpc::resetDownstreamConnection() {
  ENVOY_CONN_LOG(debug, "resetting downstream connection", parent_.read_callbacks_->connection());
  parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
