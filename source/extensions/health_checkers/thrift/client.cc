#include "source/extensions/health_checkers/thrift/client.h"

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.validate.h"
#include "envoy/extensions/health_checkers/thrift/v3/thrift.pb.h"

#include "source/extensions/filters/network/thrift_proxy/thrift.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace ThriftHealthChecker {

// SimpleResponseDecoder
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

FilterStatus SimpleResponseDecoder::transportEnd() {
  ENVOY_LOG(trace, "SimpleResponseDecoder transportEnd");
  return FilterStatus::Continue;
}

// ThriftSessionCallbacks
void ThriftSessionCallbacks::onEvent(Network::ConnectionEvent event) { parent_.onEvent(event); }

void ThriftSessionCallbacks::onAboveWriteBufferHighWatermark() {
  parent_.onAboveWriteBufferHighWatermark();
}

void ThriftSessionCallbacks::onBelowWriteBufferLowWatermark() {
  parent_.onBelowWriteBufferLowWatermark();
}

Network::FilterStatus ThriftSessionCallbacks::onData(Buffer::Instance& data, bool) {
  // Response data flow to Client and then SimpleResponseDecoder.
  parent_.onData(data);
  return Network::FilterStatus::StopIteration;
}

// Client
void Client::start() {
  ENVOY_CONN_LOG(trace, "ThriftHealthChecker Client start", *connection_);
  session_callbacks_ = std::make_unique<ThriftSessionCallbacks>(*this);
  connection_->addConnectionCallbacks(*session_callbacks_);
  connection_->addReadFilter(session_callbacks_);

  connection_->connect();
  connection_->noDelay(true);
}

bool Client::makeRequest() {
  ENVOY_CONN_LOG(trace, "ThriftHealthChecker Client makeRequest", *connection_);
  if (connection_->state() == Network::Connection::State::Closed) {
    ENVOY_CONN_LOG(debug, "ThriftHealthChecker Client makeRequest fail due to closed connection",
                   *connection_);
    return false;
  }

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
  ENVOY_CONN_LOG(trace, "ThriftHealthChecker Client makeRequest success id={}", *connection_,
                 metadata->sequenceId());
  return true;
}

void Client::close() {
  ENVOY_CONN_LOG(trace, "ThriftHealthChecker Client close", *connection_);
  connection_->close(Network::ConnectionCloseType::NoFlush);
}

void Client::onData(Buffer::Instance& data) {
  ENVOY_CONN_LOG(trace, "ThriftHealthChecker Client onData. total pending buffer={}", *connection_,
                 data.length());
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

} // namespace ThriftHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
