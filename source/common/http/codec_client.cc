#include "source/common/http/codec_client.h"

#include <cstdint>
#include <memory>

#include "envoy/http/codec.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/config/utility.h"
#include "source/common/http/exception.h"
#include "source/common/http/http1/codec_impl.h"
#include "source/common/http/http2/codec_impl.h"
#include "source/common/http/status.h"
#include "source/common/http/utility.h"

#ifdef ENVOY_ENABLE_QUIC
#include "source/common/quic/client_codec_impl.h"
#endif

namespace Envoy {
namespace Http {

CodecClient::CodecClient(CodecType type, Network::ClientConnectionPtr&& connection,
                         Upstream::HostDescriptionConstSharedPtr host,
                         Event::Dispatcher& dispatcher)
    : type_(type), host_(host), connection_(std::move(connection)),
      idle_timeout_(host_->cluster().idleTimeout()) {
  if (type_ != CodecType::HTTP3) {
    // Make sure upstream connections process data and then the FIN, rather than processing
    // TCP disconnects immediately. (see https://github.com/envoyproxy/envoy/issues/1679 for
    // details)
    connection_->detectEarlyCloseWhenReadDisabled(false);
  }
  connection_->addConnectionCallbacks(*this);
  connection_->addReadFilter(Network::ReadFilterSharedPtr{new CodecReadFilter(*this)});

  if (idle_timeout_) {
    idle_timer_ = dispatcher.createTimer([this]() -> void { onIdleTimeout(); });
    enableIdleTimer();
  }

  // We just universally set no delay on connections. Theoretically we might at some point want
  // to make this configurable.
  connection_->noDelay(true);
}

void CodecClient::connect() {
  ASSERT(!connect_called_);
  connect_called_ = true;
  ASSERT(codec_ != nullptr);
  // In general, codecs are handed new not-yet-connected connections, but in the
  // case of ALPN, the codec may be handed an already connected connection.
  if (!connection_->connecting()) {
    ASSERT(connection_->state() == Network::Connection::State::Open);
    connected_ = true;
  } else {
    ENVOY_CONN_LOG(debug, "connecting", *connection_);
    connection_->connect();
  }
}

void CodecClient::close(Network::ConnectionCloseType type) { connection_->close(type); }

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
  request->setEncoder(codec_->newStream(*request));
  LinkedList::moveIntoList(std::move(request), active_requests_);

  auto upstream_info = connection_->streamInfo().upstreamInfo();
  upstream_info->setUpstreamNumStreams(upstream_info->upstreamNumStreams() + 1);

  disableIdleTimer();
  return *active_requests_.front();
}

void CodecClient::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::Connected) {
    ENVOY_CONN_LOG(debug, "connected", *connection_);
    connected_ = true;
    return;
  }

  if (event == Network::ConnectionEvent::RemoteClose) {
    remote_closed_ = true;
  }

  // HTTP/1 can signal end of response by disconnecting. We need to handle that case.
  if (type_ == CodecType::HTTP1 && event == Network::ConnectionEvent::RemoteClose &&
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
    StreamResetReason reason = event == Network::ConnectionEvent::RemoteClose
                                   ? StreamResetReason::RemoteConnectionFailure
                                   : StreamResetReason::LocalConnectionFailure;
    if (connected_) {
      reason = StreamResetReason::ConnectionTermination;
      if (protocol_error_) {
        reason = StreamResetReason::ProtocolError;
        connection_->streamInfo().setResponseFlag(
            StreamInfo::CoreResponseFlag::UpstreamProtocolError);
      }
    }
    while (!active_requests_.empty()) {
      // Fake resetting all active streams so that reset() callbacks get invoked.
      active_requests_.front()->getStream().resetStream(reason);
    }
  }
}

void CodecClient::responsePreDecodeComplete(ActiveRequest& request) {
  ENVOY_CONN_LOG(debug, "response complete", *connection_);
  if (codec_client_callbacks_) {
    codec_client_callbacks_->onStreamPreDecodeComplete();
  }
  request.decode_complete_ = true;
  if (request.encode_complete_ || !request.wait_encode_complete_) {
    completeRequest(request);
  } else {
    ENVOY_CONN_LOG(debug, "waiting for encode to complete", *connection_);
  }
}

void CodecClient::requestEncodeComplete(ActiveRequest& request) {
  ENVOY_CONN_LOG(debug, "encode complete", *connection_);
  request.encode_complete_ = true;
  if (request.decode_complete_) {
    completeRequest(request);
  }
}

void CodecClient::completeRequest(ActiveRequest& request) {
  deleteRequest(request);

  // HTTP/2 can send us a reset after a complete response if the request was not complete. Users
  // of CodecClient will deal with the premature response case and we should not handle any
  // further reset notification.
  request.removeEncoderCallbacks();
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

    // Don't count 408 responses where we have no active requests as protocol errors.
    // Don't count graceful GOAWAY closes.
    const bool not_408 =
        !isPrematureResponseError(status) ||
        (!active_requests_.empty() || getPrematureResponseHttpCode(status) != Code::RequestTimeout);
    const bool is_goaway = isGoAwayGracefulCloseError(status);
    if (not_408 &&
        (!is_goaway || !Runtime::runtimeFeatureEnabled(
                           "envoy.reloadable_features.http2_no_protocol_error_upon_clean_close"))) {
      host_->cluster().trafficStats()->upstream_cx_protocol_error_.inc();
      protocol_error_ = true;
    }
    close();
  }

  // All data should be consumed at this point if the connection remains open.
  ASSERT(data.length() == 0 || connection_->state() != Network::Connection::State::Open,
         absl::StrCat("extraneous bytes after response complete: ", data.length()));
}

Status CodecClient::ActiveRequest::encodeHeaders(const RequestHeaderMap& headers, bool end_stream) {
#ifdef ENVOY_ENABLE_UHV
  if (header_validator_) {
    bool failure = false;
    std::string failure_details;
    auto transformation_result = header_validator_->transformRequestHeaders(headers);
    if (!transformation_result.status.ok()) {
      ENVOY_CONN_LOG(debug, "Request header transformation failed: {}\n{}", *parent_.connection_,
                     transformation_result.status.details(), headers);
      failure = true;
      failure_details = std::string(transformation_result.status.details());
    } else {
      // Validate header map after request encoder transformations
      const ::Envoy::Http::HeaderValidator::ValidationResult validation_result =
          header_validator_->validateRequestHeaders(
              transformation_result.new_headers ? *transformation_result.new_headers : headers);
      if (!validation_result.ok()) {
        ENVOY_CONN_LOG(debug, "Request header validation failed: {}\n{}", *parent_.connection_,
                       validation_result.details(),
                       transformation_result.new_headers ? *transformation_result.new_headers
                                                         : headers);
        failure = true;
        failure_details = std::string(validation_result.details());
      }
    }
    if (failure) {
      return absl::InvalidArgumentError(
          absl::StrCat("header validation failed: ", failure_details));
    }
    return RequestEncoderWrapper::encodeHeaders(
        transformation_result.new_headers ? *transformation_result.new_headers : headers,
        end_stream);
  }
#endif
  return RequestEncoderWrapper::encodeHeaders(headers, end_stream);
}

void CodecClient::ActiveRequest::decodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) {
#ifdef ENVOY_ENABLE_UHV
  if (header_validator_) {
    const ::Envoy::Http::HeaderValidator::ValidationResult validation_result =
        header_validator_->validateResponseHeaders(*headers);
    bool failure = !validation_result.ok();
    std::string failure_details(validation_result.details());
    if (!failure) {
      const ::Envoy::Http::ClientHeaderValidator::TransformationResult transformation_result =
          header_validator_->transformResponseHeaders(*headers);
      failure = !transformation_result.ok();
      failure_details = std::string(validation_result.details());
    }
    if (failure) {
      ENVOY_CONN_LOG(debug, "Response header validation failed: {}\n{}", *parent_.connection_,
                     failure_details, *headers);
      if ((parent_.codec_->protocol() == Protocol::Http2 &&
           !parent_.host_->cluster()
                .http2Options()
                .override_stream_error_on_invalid_http_message()
                .value()) ||
          (parent_.codec_->protocol() == Protocol::Http3 &&
           !parent_.host_->cluster()
                .http3Options()
                .override_stream_error_on_invalid_http_message()
                .value())) {
        parent_.protocol_error_ = true;
        parent_.close();
      } else {
        inner_encoder_->getStream().resetStream(StreamResetReason::ProtocolError);
      }
      return;
    }
  }
#endif
  ResponseDecoderWrapper::decodeHeaders(std::move(headers), end_stream);
}

CodecClientProd::CodecClientProd(CodecType type, Network::ClientConnectionPtr&& connection,
                                 Upstream::HostDescriptionConstSharedPtr host,
                                 Event::Dispatcher& dispatcher,
                                 Random::RandomGenerator& random_generator,
                                 const Network::TransportSocketOptionsConstSharedPtr& options,
                                 bool should_connect)
    : CodecClient(type, std::move(connection), host, dispatcher) {
  switch (type) {
  case CodecType::HTTP1: {
    // If the transport socket indicates this is being proxied, inform the HTTP/1.1 codec. It will
    // send fully qualified URLs iff the underlying transport is plaintext.
    bool proxied = false;
    if (options && options->http11ProxyInfo().has_value()) {
      proxied = true;
    }
    codec_ = std::make_unique<Http1::ClientConnectionImpl>(
        *connection_, host->cluster().http1CodecStats(), *this, host->cluster().http1Settings(),
        host->cluster().maxResponseHeadersKb(), host->cluster().maxResponseHeadersCount(), proxied);
    break;
  }
  case CodecType::HTTP2:
    codec_ = std::make_unique<Http2::ClientConnectionImpl>(
        *connection_, *this, host->cluster().http2CodecStats(), random_generator,
        host->cluster().http2Options(),
        host->cluster().maxResponseHeadersKb().value_or(Http::DEFAULT_MAX_REQUEST_HEADERS_KB),
        host->cluster().maxResponseHeadersCount(), Http2::ProdNghttp2SessionFactory::get());
    break;
  case CodecType::HTTP3: {
#ifdef ENVOY_ENABLE_QUIC
    auto& quic_session = dynamic_cast<Quic::EnvoyQuicClientSession&>(*connection_);
    codec_ = std::make_unique<Quic::QuicHttpClientConnectionImpl>(
        quic_session, *this, host->cluster().http3CodecStats(), host->cluster().http3Options(),
        host->cluster().maxResponseHeadersKb().value_or(Http::DEFAULT_MAX_REQUEST_HEADERS_KB),
        host->cluster().maxResponseHeadersCount());
    // Initialize the session after max request header size is changed in above http client
    // connection creation.
    quic_session.Initialize();
    break;
#else
    // Should be blocked by configuration checking at an earlier point.
    PANIC("unexpected");
#endif
  }
  }
  if (should_connect) {
    connect();
  }
}

} // namespace Http
} // namespace Envoy
