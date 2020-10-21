#include "common/http/codec_client.h"

#include <cstdint>
#include <memory>

#include "envoy/http/codec.h"

#include "common/common/enum_to_int.h"
#include "common/config/utility.h"
#include "common/http/exception.h"
#include "common/http/http1/codec_impl.h"
#include "common/http/http1/codec_impl_legacy.h"
#include "common/http/http2/codec_impl.h"
#include "common/http/http2/codec_impl_legacy.h"
#include "common/http/http3/quic_codec_factory.h"
#include "common/http/http3/well_known_names.h"
#include "common/http/status.h"
#include "common/http/utility.h"
#include "common/runtime/runtime_features.h"
#include "common/runtime/runtime_impl.h"

namespace Envoy {
namespace Http {

CodecClient::CodecClient(Type type, Network::ClientConnectionPtr&& connection,
                         Upstream::HostDescriptionConstSharedPtr host,
                         Event::Dispatcher& dispatcher)
    : type_(type), host_(host), connection_(std::move(connection)),
      idle_timeout_(host_->cluster().idleTimeout()) {
  if (type_ != Type::HTTP3) {
    // Make sure upstream connections process data and then the FIN, rather than processing
    // TCP disconnects immediately. (see https://github.com/envoyproxy/envoy/issues/1679 for
    // details)
    connection_->detectEarlyCloseWhenReadDisabled(false);
  }
  connection_->addConnectionCallbacks(*this);
  connection_->addReadFilter(Network::ReadFilterSharedPtr{new CodecReadFilter(*this)});

  ENVOY_CONN_LOG(debug, "connecting", *connection_);
  connection_->connect();

  if (idle_timeout_) {
    idle_timer_ = dispatcher.createTimer([this]() -> void { onIdleTimeout(); });
    enableIdleTimer();
  }

  // We just universally set no delay on connections. Theoretically we might at some point want
  // to make this configurable.
  connection_->noDelay(true);
}

CodecClient::~CodecClient() = default;

void CodecClient::close() { connection_->close(Network::ConnectionCloseType::NoFlush); }

void CodecClient::deleteRequest(ActiveRequest& request) {
  connection_->dispatcher().deferredDelete(request.removeFromList(active_requests_));
  if (codec_client_callbacks_) {
    codec_client_callbacks_->onStreamDestroy();
  }
  if (numActiveRequests() == 0) {
    enableIdleTimer();
  }
}

RequestEncoder& CodecClient::newStream(ResponseDecoder& response_decoder) {
  ActiveRequestPtr request(new ActiveRequest(*this, response_decoder));
  request->encoder_ = &codec_->newStream(*request);
  request->encoder_->getStream().addCallbacks(*request);
  LinkedList::moveIntoList(std::move(request), active_requests_);
  disableIdleTimer();
  return *active_requests_.front()->encoder_;
}

void CodecClient::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::Connected) {
    ENVOY_CONN_LOG(debug, "connected", *connection_);
    connection_->streamInfo().setDownstreamSslConnection(connection_->ssl());
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
    disableIdleTimer();
    idle_timer_.reset();
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
  const Status status = codec_->dispatch(data);

  if (!status.ok()) {
    ENVOY_CONN_LOG(debug, "Error dispatching received data: {}", *connection_, status.message());
    close();

    // Don't count 408 responses where we have no active requests as protocol errors
    if (!isPrematureResponseError(status) ||
        (!active_requests_.empty() ||
         getPrematureResponseHttpCode(status) != Code::RequestTimeout)) {
      host_->cluster().stats().upstream_cx_protocol_error_.inc();
    }
  }
}

CodecClientProd::CodecClientProd(Type type, Network::ClientConnectionPtr&& connection,
                                 Upstream::HostDescriptionConstSharedPtr host,
                                 Event::Dispatcher& dispatcher,
                                 Random::RandomGenerator& random_generator)
    : CodecClient(type, std::move(connection), host, dispatcher) {

  switch (type) {
  case Type::HTTP1: {
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.new_codec_behavior")) {
      codec_ = std::make_unique<Http1::ClientConnectionImpl>(
          *connection_, host->cluster().http1CodecStats(), *this, host->cluster().http1Settings(),
          host->cluster().maxResponseHeadersCount());
    } else {
      codec_ = std::make_unique<Legacy::Http1::ClientConnectionImpl>(
          *connection_, host->cluster().http1CodecStats(), *this, host->cluster().http1Settings(),
          host->cluster().maxResponseHeadersCount());
    }
    break;
  }
  case Type::HTTP2: {
    codec_ = std::make_unique<Http2::ClientConnectionImpl>(
        *connection_, *this, host->cluster().http2CodecStats(), random_generator,
        host->cluster().http2Options(), Http::DEFAULT_MAX_REQUEST_HEADERS_KB,
        host->cluster().maxResponseHeadersCount(), Http2::ProdNghttp2SessionFactory::get());
    break;
  }
  case Type::HTTP3: {
    codec_ = std::unique_ptr<ClientConnection>(
        Config::Utility::getAndCheckFactoryByName<Http::QuicHttpClientConnectionFactory>(
            Http::QuicCodecNames::get().Quiche)
            .createQuicClientConnection(*connection_, *this));
    break;
  }
  }
}

} // namespace Http
} // namespace Envoy
