#include "codec_client.h"

#include "common/common/enum_to_int.h"
#include "common/http/exception.h"
#include "common/http/http1/codec_impl.h"
#include "common/http/http2/codec_impl.h"
#include "common/http/utility.h"

namespace Http {

CodecClient::CodecClient(Type type, Network::ClientConnectionPtr&& connection,
                         const CodecClientStats& stats)
    : type_(type), connection_(std::move(connection)), stats_(stats) {

  connection_->addConnectionCallbacks(*this);
  connection_->addReadFilter(Network::ReadFilterPtr{new CodecReadFilter(*this)});

  conn_log_info("connecting", *connection_);
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

Http::StreamEncoder& CodecClient::newStream(Http::StreamDecoder& response_decoder) {
  ActiveRequestPtr request(new ActiveRequest(*this));
  request->decoder_wrapper_.reset(new ResponseDecoderWrapper(response_decoder, *request));
  request->encoder_ = &codec_->newStream(*request->decoder_wrapper_);
  request->encoder_->getStream().addCallbacks(*request);
  request->moveIntoList(std::move(request), active_requests_);
  return *active_requests_.front()->encoder_;
}

void CodecClient::onEvent(uint32_t events) {
  if (events & Network::ConnectionEvent::Connected) {
    conn_log_debug("connected", *connection_);
    connected_ = true;
  }

  // HTTP/1 can signal end of response by disconnecting. We need to handle that case.
  if (type_ == Type::HTTP1 && (events & Network::ConnectionEvent::RemoteClose) &&
      !active_requests_.empty()) {
    Buffer::OwnedImpl empty;
    onData(empty);
  }

  if ((events & Network::ConnectionEvent::RemoteClose) ||
      (events & Network::ConnectionEvent::LocalClose)) {
    conn_log_debug("disconnect. resetting {} pending requests", *connection_,
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
  conn_log_debug("response complete", *connection_);
  deleteRequest(request);

  // HTTP/2 can send us a reset after a complete response if the request was not complete. Users
  // of CodecClient will deal with the premature response case and we should not handle any
  // further reset notification.
  request.encoder_->getStream().removeCallbacks(request);
}

void CodecClient::onReset(ActiveRequest& request, Http::StreamResetReason reason) {
  conn_log_debug("request reset", *connection_);
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
    conn_log_info("protocol error: {}", *connection_, e.what());
    close();
    protocol_error = true;
  } catch (PrematureResponseException& e) {
    conn_log_info("premature response", *connection_);
    close();

    // Don't count 408 responses where we have no active requests as protocol errors
    if (!active_requests_.empty() ||
        Utility::getResponseStatus(e.headers()) != enumToInt(Http::Code::RequestTimeout)) {
      protocol_error = true;
    }
  }

  if (protocol_error) {
    stats_.upstream_cx_protocol_error_.inc();
  }
}

CodecClientProd::CodecClientProd(Type type, Network::ClientConnectionPtr&& connection,
                                 const CodecClientStats& stats, Stats::Store& store,
                                 uint64_t codec_options)
    : CodecClient(type, std::move(connection), stats) {
  switch (type) {
  case Type::HTTP1: {
    codec_.reset(new Http::Http1::ClientConnectionImpl(*connection_, *this));
    break;
  }
  case Type::HTTP2: {
    codec_.reset(new Http::Http2::ClientConnectionImpl(*connection_, *this, store, codec_options));
    break;
  }
  }
}

} // Http
