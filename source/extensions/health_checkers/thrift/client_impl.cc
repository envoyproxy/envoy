#include "source/extensions/health_checkers/thrift/client_impl.h"

#include "source/extensions/filters/network/thrift_proxy/thrift.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace ThriftHealthChecker {

bool SimpleResponseDecoder::onData(Buffer::Instance& data) {
  buffer_.move(data);

  bool underflow = false;
  decoder_->onData(buffer_, underflow);
  ASSERT(complete_ || underflow);

  return complete_;
}

bool SimpleResponseDecoder::responseSuccess() {
  ENVOY_LOG(trace, "SimpleResponseDecoder responseSuccess complete={} success={}", complete_,
            success_.has_value() && success_.value());
  return complete_ && success_.has_value() && success_.value();
}

FilterStatus SimpleResponseDecoder::messageBegin(MessageMetadataSharedPtr metadata) {
  ENVOY_LOG(trace, "SimpleResponseDecoder messageBegin message_type={} reply_type={}",
            metadata->hasMessageType() ? MessageTypeNames::get().fromType(metadata->messageType())
                                       : "-",
            metadata->hasReplyType() ? ReplyTypeNames::get().fromType(metadata->replyType()) : "-");

  if (metadata->hasReplyType()) {
    success_ = metadata->replyType() == ReplyType::Success;
  }

  if (metadata->hasMessageType() && metadata->messageType() == MessageType::Exception) {
    success_ = false;
  }
  return FilterStatus::Continue;
}

FilterStatus SimpleResponseDecoder::messageEnd() {
  ENVOY_LOG(trace, "SimpleResponseDecoder messageEnd");
  complete_ = true;
  return FilterStatus::Continue;
}

void ThriftSessionCallbacks::onEvent(Network::ConnectionEvent event) { parent_.onEvent(event); }

void ThriftSessionCallbacks::onAboveWriteBufferHighWatermark() {
  parent_.onAboveWriteBufferHighWatermark();
}

void ThriftSessionCallbacks::onBelowWriteBufferLowWatermark() {
  parent_.onBelowWriteBufferLowWatermark();
}

Network::FilterStatus ThriftSessionCallbacks::onData(Buffer::Instance& data, bool) {
  parent_.onData(data);
  return Network::FilterStatus::StopIteration;
}

void ClientImpl::start() {
  Upstream::Host::CreateConnectionData conn_data = parent_.createConnection();
  connection_ = std::move(conn_data.connection_);
  host_description_ = std::move(conn_data.host_description_);
  session_callbacks_ = std::make_unique<ThriftSessionCallbacks>(*this);
  connection_->addConnectionCallbacks(*session_callbacks_);
  connection_->addReadFilter(session_callbacks_);

  connection_->connect();
  connection_->noDelay(true);

  ENVOY_CONN_LOG(trace, "ThriftHealthChecker ClientImpl start", *connection_);
}

bool ClientImpl::sendRequest() {
  ENVOY_CONN_LOG(trace, "ThriftHealthChecker ClientImpl sendRequest", *connection_);
  ASSERT(connection_->state() == Network::Connection::State::Open);

  Buffer::OwnedImpl request_buffer;
  ProtocolConverterSharedPtr protocol_converter = std::make_shared<ProtocolConverter>();
  ProtocolPtr protocol = createProtocol();
  protocol_converter->initProtocolConverter(*protocol, request_buffer);

  MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
  metadata->setProtocol(protocol_);
  metadata->setMethodName(method_name_);
  metadata->setMessageType(MessageType::Call);
  metadata->setSequenceId(sequenceId());

  protocol_converter->messageBegin(metadata);
  protocol_converter->structBegin("");
  FieldType field_type_stop = FieldType::Stop;
  int16_t field_id = 0;
  protocol_converter->fieldBegin("", field_type_stop, field_id);
  protocol_converter->structEnd();
  protocol_converter->messageEnd();

  TransportPtr transport = createTransport();
  Buffer::OwnedImpl transport_buffer;
  transport->encodeFrame(transport_buffer, *metadata, request_buffer);

  connection_->write(transport_buffer, false);
  ENVOY_CONN_LOG(trace, "ThriftHealthChecker ClientImpl sendRequest success id={}", *connection_,
                 metadata->sequenceId());
  return true;
}

void ClientImpl::close() {
  ENVOY_CONN_LOG(trace, "ThriftHealthChecker ClientImpl close", *connection_);
  connection_->close(Network::ConnectionCloseType::NoFlush);
}

void ClientImpl::onData(Buffer::Instance& data) {
  ENVOY_CONN_LOG(trace, "ThriftHealthChecker ClientImpl onData. total pending buffer={}",
                 *connection_, data.length());
  if (!response_decoder_) {
    response_decoder_ =
        std::make_unique<SimpleResponseDecoder>(createTransport(), createProtocol());
  }
  if (response_decoder_->onData(data)) {
    ENVOY_CONN_LOG(trace, "Response complete. Result={} ", *connection_,
                   response_decoder_->responseSuccess());
    parent_.onResponseResult(response_decoder_->responseSuccess());
  }
}

ClientFactoryImpl ClientFactoryImpl::instance_;

ClientPtr ClientFactoryImpl::create(ClientCallback& callbacks, TransportType transport,
                                    ProtocolType protocol, const std::string& method_name,
                                    Upstream::HostSharedPtr host, int32_t seq_id,
                                    bool fixed_seq_id) {
  auto client = std::make_unique<ClientImpl>(callbacks, transport, protocol, method_name,
                                             std::move(host), seq_id, fixed_seq_id);
  return client;
}

} // namespace ThriftHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
