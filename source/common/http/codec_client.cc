#include "common/http/codec_client.h"

#include <cstdint>

#include "common/common/enum_to_int.h"
#include "common/http/exception.h"
#include "common/http/http1/codec_impl.h"
#include "common/http/http2/codec_impl.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Http {

CodecClient::CodecClient(Type type, Network::ClientConnectionPtr&& connection,
                         Upstream::HostDescriptionConstSharedPtr host)
    : type_(type), connection_(std::move(connection)), host_(host) {
  // Make sure upstream connections process data and then the FIN, rather than processing
  // TCP disconnects immediately. (see https://github.com/envoyproxy/envoy/issues/1679 for details)
  connection_->detectEarlyCloseWhenReadDisabled(false);
  connection_->addConnectionCallbacks(*this);
  connection_->addReadFilter(Network::ReadFilterSharedPtr{new CodecReadFilter(*this)});

  ENVOY_CONN_LOG(debug, "connecting", *connection_);
  connection_->connect();

  // We just universally set no delay on connections. Theoretically we might at some point want
  // to make this configurable.
  connection_->noDelay(true);
}

CodecClient::~CodecClient() {}

void CodecClient::close() { connection_->close(Network::ConnectionCloseType::NoFlush); }

void CodecClient::deleteRequest(ActiveRequest& request) {
  connection_->dispatcher().deferredDelete(request.removeFromList(active_requests_));
  if (codec_client_callbacks_) {
    codec_client_callbacks_->onStreamDestroy();
  }
}

StreamEncoder& CodecClient::newStream(StreamDecoder& response_decoder) {
  ActiveRequestPtr request(new ActiveRequest(*this, response_decoder));
  request->encoder_ = &codec_->newStream(*request);
  request->encoder_->getStream().addCallbacks(*request);
  request->moveIntoList(std::move(request), active_requests_);
  return *active_requests_.front()->encoder_;
}

void CodecClient::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::Connected) {
    ENVOY_CONN_LOG(debug, "connected", *connection_);
    connected_ = true;
  }

  if (event == Network::ConnectionEvent::RemoteClose) {
    remote_closed_ = true;
  }

  // HTTP/1 can signal end of response by disconnecting. We need to handle that case.
  if (type_ == Type::HTTP1 && event == Network::ConnectionEvent::RemoteClose &&
      !active_requests_.empty()) {
    Buffer::OwnedImpl empty;
    onData(empty);
  }

  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    ENVOY_CONN_LOG(debug, "disconnect. resetting {} pending requests", *connection_,
                   active_requests_.size());
    while (!active_requests_.empty()) {
      // Fake resetting all active streams so that reset() callbacks get invoked.
      active_requests_.front()->encoder_->getStream().resetStream(
          connected_ ? StreamResetReason::ConnectionTermination
                     : StreamResetReason::ConnectionFailure);
    }
  }
}

void CodecClient::responseDecodeComplete(ActiveRequest& request) {
  ENVOY_CONN_LOG(debug, "response complete", *connection_);
  deleteRequest(request);

  // HTTP/2 can send us a reset after a complete response if the request was not complete. Users
  // of CodecClient will deal with the premature response case and we should not handle any
  // further reset notification.
  request.encoder_->getStream().removeCallbacks(request);
}

void CodecClient::onReset(ActiveRequest& request, StreamResetReason reason) {
  ENVOY_CONN_LOG(debug, "request reset", *connection_);
  if (codec_client_callbacks_) {
    codec_client_callbacks_->onStreamReset(reason);
  }

  deleteRequest(request);
}

void CodecClient::onData(Buffer::Instance& data) {
  bool protocol_error = false;
  try {
    codec_->dispatch(data);
  } catch (CodecProtocolException& e) {
    ENVOY_CONN_LOG(info, "protocol error: {}", *connection_, e.what());
    close();
    protocol_error = true;
  } catch (PrematureResponseException& e) {
    ENVOY_CONN_LOG(info, "premature response", *connection_);
    close();

    // Don't count 408 responses where we have no active requests as protocol errors
    if (!active_requests_.empty() ||
        Utility::getResponseStatus(e.headers()) != enumToInt(Code::RequestTimeout)) {
      protocol_error = true;
    }
  }

  if (protocol_error) {
    host_->cluster().stats().upstream_cx_protocol_error_.inc();
  }
}

CodecClientProd::CodecClientProd(Type type, Network::ClientConnectionPtr&& connection,
                                 Upstream::HostDescriptionConstSharedPtr host)
    : CodecClient(type, std::move(connection), host) {
  switch (type) {
  case Type::HTTP1: {
    codec_.reset(new Http1::ClientConnectionImpl(*connection_, *this));
    break;
  }
  case Type::HTTP2: {
    codec_.reset(new Http2::ClientConnectionImpl(*connection_, *this, host->cluster().statsScope(),
                                                 host->cluster().http2Settings()));
    break;
  }
  }
}

} // namespace Http
} // namespace Envoy
