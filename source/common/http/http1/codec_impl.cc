#include "codec_impl.h"

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"

#include "common/common/utility.h"
#include "common/http/codes.h"
#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

namespace Http {
namespace Http1 {

const std::string StreamEncoderImpl::CRLF = "\r\n";
const std::string StreamEncoderImpl::LAST_CHUNK = "0\r\n\r\n";

void StreamEncoderImpl::encodeHeader(const std::string& key, const std::string& value) {
  output_buffer_.add(key);
  output_buffer_.add(": ");
  output_buffer_.add(value);
  output_buffer_.add(CRLF);
}

void StreamEncoderImpl::encodeHeaders(const HeaderMap& headers, bool end_stream) {
  bool saw_content_length = false;
  bool saw_chunk_encoding = false;
  headers.iterate([this, &saw_content_length, &saw_chunk_encoding](
                      const LowerCaseString& key, const std::string& value) -> void {
    const LowerCaseString* key_to_use = &key;
    // Translate :host -> host so that upper layers do not need to deal with this.
    if (Headers::get().Host == *key_to_use) {
      key_to_use = &Headers::get().HostLegacy;
    }

    // Skip all headers starting with ':' that make it here.
    if (key_to_use->get()[0] == ':') {
      return;
    }

    if (Headers::get().ContentLength == *key_to_use) {
      saw_content_length = true;
    }

    if (Headers::get().TransferEncoding == *key_to_use &&
        0 == StringUtil::caseInsensitiveCompare(Headers::get().TransferEncodingValues.Chunked,
                                                value)) {
      saw_chunk_encoding = true;
    }

    encodeHeader(key_to_use->get(), value);
  });

  // Assume we are chunk encoding unless we are passed a content length or this is a header only
  // response. Upper layers generally should strip transfer-encoding since it only applies to
  // HTTP/1.1. The codec will infer it based on the type of response.
  if (saw_content_length) {
    ASSERT(!saw_chunk_encoding);
    chunk_encoding_ = false;
  } else if (saw_chunk_encoding) {
    // HEAD request responses can supply transfer-encoding but we still do a header only response.
    chunk_encoding_ = !end_stream;
  } else {
    if (end_stream) {
      encodeHeader(Headers::get().ContentLength.get(), "0");
      chunk_encoding_ = false;
    } else {
      encodeHeader(Headers::get().TransferEncoding.get(),
                   Headers::get().TransferEncodingValues.Chunked);
      chunk_encoding_ = true;
    }
  }

  output_buffer_.add(CRLF);

  if (end_stream) {
    endEncode();
  } else {
    flushOutput();
  }
}

void StreamEncoderImpl::encodeData(Buffer::Instance& data, bool end_stream) {
  // end_stream may be indicated with a zero length data buffer. If that is the case, so not
  // atually write the zero length buffer out.
  if (data.length() > 0) {
    if (chunk_encoding_) {
      output_buffer_.add(fmt::format("{:x}\r\n", data.length()));
    }

    output_buffer_.move(data);

    if (chunk_encoding_) {
      output_buffer_.add(CRLF);
    }
  }

  if (end_stream) {
    endEncode();
  } else {
    flushOutput();
  }
}

void StreamEncoderImpl::encodeTrailers(const HeaderMap&) { endEncode(); }

void StreamEncoderImpl::endEncode() {
  if (chunk_encoding_) {
    output_buffer_.add(LAST_CHUNK);
  }

  flushOutput();
  connection_.onEncodeComplete();
}

void StreamEncoderImpl::flushOutput() {
  connection_.connection().write(output_buffer_);
  output_buffer_.drain(output_buffer_.length());
}

void StreamEncoderImpl::resetStream(StreamResetReason reason) {
  connection_.onResetStreamBase(reason);
}

void ResponseStreamEncoderImpl::encodeHeaders(const HeaderMap& headers, bool end_stream) {
  started_response_ = true;
  uint64_t numeric_status = Utility::getResponseStatus(headers);

  output_buffer_.add("HTTP/1.1 ");
  output_buffer_.add(std::to_string(numeric_status));
  output_buffer_.add(" ");
  output_buffer_.add(CodeUtility::toString(static_cast<Code>(numeric_status)));
  output_buffer_.add(CRLF);
  StreamEncoderImpl::encodeHeaders(headers, end_stream);
}

void RequestStreamEncoderImpl::encodeHeaders(const HeaderMap& headers, bool end_stream) {
  const std::string& method = headers.get(Headers::get().Method);
  const std::string& path = headers.get(Headers::get().Path);
  if (method.empty() || path.empty()) {
    throw CodecClientException(":method and :path must be specified");
  }

  if (method == Headers::get().MethodValues.Head) {
    head_request_ = true;
  }

  output_buffer_.add(method);
  output_buffer_.add(" ");
  output_buffer_.add(path);
  output_buffer_.add(" HTTP/1.1\r\n");
  StreamEncoderImpl::encodeHeaders(headers, end_stream);
}

http_parser_settings ConnectionImpl::settings_{
    [](http_parser* parser) -> int {
      static_cast<ConnectionImpl*>(parser->data)->onMessageBeginBase();
      return 0;
    },
    [](http_parser* parser, const char* at, size_t length) -> int {
      static_cast<ConnectionImpl*>(parser->data)->onUrl(at, length);
      return 0;
    },
    nullptr, // on_status
    [](http_parser* parser, const char* at, size_t length) -> int {
      static_cast<ConnectionImpl*>(parser->data)->onHeaderField(at, length);
      return 0;
    },
    [](http_parser* parser, const char* at, size_t length) -> int {
      static_cast<ConnectionImpl*>(parser->data)->onHeaderValue(at, length);
      return 0;
    },
    [](http_parser* parser)
        -> int { return static_cast<ConnectionImpl*>(parser->data)->onHeadersCompleteBase(); },
    [](http_parser* parser, const char* at, size_t length) -> int {
      static_cast<ConnectionImpl*>(parser->data)->onBody(at, length);
      return 0;
    },
    [](http_parser* parser) -> int {
      static_cast<ConnectionImpl*>(parser->data)->onMessageComplete();
      return 0;
    },
    nullptr, // on_chunk_header
    nullptr  // on_chunk_complete
};

ConnectionImpl::ConnectionImpl(Network::Connection& connection, http_parser_type type)
    : connection_(connection) {
  http_parser_init(&parser_, type);
  parser_.data = this;
}

ConnectionImpl::~ConnectionImpl() {
  // needed to avoid unique_ptr forward decl issues.
}

void ConnectionImpl::completeLastHeader() {
  conn_log_trace("completed header: key={} value={}", connection_, current_header_field_,
                 current_header_value_);
  if (!current_header_field_.empty()) {
    // Handle translation from 'host' to ':host' here so upper layers don't need to deal with it.
    if (0 == StringUtil::caseInsensitiveCompare(current_header_field_,
                                                Headers::get().HostLegacy.get())) {
      current_header_field_ = Headers::get().Host.get();
    }

    current_header_map_->addViaMove(LowerCaseString(std::move(current_header_field_)),
                                    std::move(current_header_value_));
  }

  header_parsing_state_ = HeaderParsingState::Field;
  ASSERT(current_header_field_.empty());
  ASSERT(current_header_value_.empty());
}

void ConnectionImpl::dispatch(Buffer::Instance& data) {
  conn_log_trace("parsing {} bytes", connection_, data.length());

  // Always unpause before dispatch.
  http_parser_pause(&parser_, 0);

  ssize_t total_parsed = 0;
  if (data.length() > 0) {
    uint64_t num_slices = data.getRawSlices(nullptr, 0);
    Buffer::RawSlice slices[num_slices];
    data.getRawSlices(slices, num_slices);
    for (Buffer::RawSlice& slice : slices) {
      total_parsed += dispatchSlice(static_cast<const char*>(slice.mem_), slice.len_);
    }
  } else {
    dispatchSlice(nullptr, 0);
  }

  conn_log_trace("parsed {} bytes", connection_, total_parsed);
  data.drain(total_parsed);
}

size_t ConnectionImpl::dispatchSlice(const char* slice, size_t len) {
  ssize_t rc = http_parser_execute(&parser_, &settings_, slice, len);
  if (HTTP_PARSER_ERRNO(&parser_) != HPE_OK && HTTP_PARSER_ERRNO(&parser_) != HPE_PAUSED) {
    sendProtocolError();
    throw CodecProtocolException("http/1.1 protocol error: " +
                                 std::string(http_errno_name(HTTP_PARSER_ERRNO(&parser_))));
  }

  return rc;
}

void ConnectionImpl::onHeaderField(const char* data, size_t length) {
  if (header_parsing_state_ == HeaderParsingState::Done) {
    // Ignore trailers.
    return;
  }

  if (header_parsing_state_ == HeaderParsingState::Value) {
    completeLastHeader();
  }

  current_header_field_.append(data, length);
}

void ConnectionImpl::onHeaderValue(const char* data, size_t length) {
  if (header_parsing_state_ == HeaderParsingState::Done) {
    // Ignore trailers.
    return;
  }

  header_parsing_state_ = HeaderParsingState::Value;
  current_header_value_.append(data, length);
}

int ConnectionImpl::onHeadersCompleteBase() {
  conn_log_trace("headers complete", connection_);
  completeLastHeader();
  if (parser_.http_major == 1 && parser_.http_minor == 1) {
    current_header_map_->addViaMoveValue(Headers::get().Version, "HTTP/1.1");
  } else {
    // This is not necessarily true, but it's good enough since higher layers only care if this is
    // HTTP/1.1 or not.
    current_header_map_->addViaMoveValue(Headers::get().Version, "HTTP/1.0");
  }

  int rc = onHeadersComplete(std::move(current_header_map_));
  current_header_map_.reset();
  header_parsing_state_ = HeaderParsingState::Done;
  return rc;
}

void ConnectionImpl::onMessageBeginBase() {
  ASSERT(!current_header_map_);
  current_header_map_.reset(new HeaderMapImpl());
  header_parsing_state_ = HeaderParsingState::Field;
  onMessageBegin();
}

void ConnectionImpl::onResetStreamBase(StreamResetReason reason) {
  ASSERT(!reset_stream_called_);
  reset_stream_called_ = true;
  onResetStream(reason);
}

ServerConnectionImpl::ServerConnectionImpl(Network::Connection& connection,
                                           ServerConnectionCallbacks& callbacks)
    : ConnectionImpl(connection, HTTP_REQUEST), callbacks_(callbacks) {}

void ServerConnectionImpl::onEncodeComplete() {
  ASSERT(active_request_);
  if (active_request_->remote_complete_) {
    // Only do this if remote is complete. If we are replying before the request is complete the
    // only logical thing to do is for higher level code to reset() / close the connection so we
    // leave the request around so that it can fire reset callbacks.
    active_request_.reset();
  }
}

int ServerConnectionImpl::onHeadersComplete(HeaderMapPtr&& headers) {
  // Handle the case where response happens prior to request complete. It's up to upper layer code
  // to disconnect the connection but we shouldn't fire any more events since it doesn't make
  // sense.
  if (active_request_) {
    headers->addViaMoveValue(Headers::get().Path, std::move(active_request_->request_url_));
    ASSERT(active_request_->request_url_.empty());

    headers->addViaMoveValue(Headers::get().Method,
                             http_method_str(static_cast<http_method>(parser_.method)));

    // Deal with expect: 100-continue here since a) only HTTP/1.1 has this, b) higher layers are
    // never going to do anything other than say to continue since we can response before request
    // complete if necessary.
    if (0 == StringUtil::caseInsensitiveCompare(headers->get(Headers::get().Expect),
                                                Headers::get().ExpectValues._100Continue)) {
      Buffer::OwnedImpl continue_response("HTTP/1.1 100 Continue\r\n\r\n");
      connection_.write(continue_response);
      headers->remove(Headers::get().Expect);
    }

    // Determine here whether we have a body or not. This uses the new RFC semantics where the
    // presence of content-length or chunked transfer-encoding indicates a body vs. a particular
    // method. If there is no body, we defer raising decodeHeaders() until the parser is flushed
    // with message complete. This allows upper layers to behave like HTTP/2 and prevents a proxy
    // scenario where the higher layers stream through and implicitly switch to chunked transfer
    // encoding because end stream with zero body length has not yet been indicated.
    if (parser_.flags & F_CHUNKED ||
        (parser_.content_length > 0 && parser_.content_length != ULLONG_MAX)) {
      active_request_->request_decoder_->decodeHeaders(std::move(headers), false);

      // If the connection has been closed (or is closing) after decoding headers, pause the parser
      // so we return control to the caller.
      if (connection_.state() != Network::Connection::State::Open) {
        http_parser_pause(&parser_, 1);
      }

    } else {
      deferred_end_stream_headers_ = std::move(headers);
    }
  }

  return 0;
}

void ServerConnectionImpl::onMessageBegin() {
  if (!resetStreamCalled()) {
    ASSERT(!active_request_);
    active_request_.reset(new ActiveRequest(*this));
    active_request_->request_decoder_ = &callbacks_.newStream(active_request_->response_encoder_);
  }
}

void ServerConnectionImpl::onUrl(const char* data, size_t length) {
  if (active_request_) {
    active_request_->request_url_.append(data, length);
  }
}

void ServerConnectionImpl::onBody(const char* data, size_t length) {
  ASSERT(!deferred_end_stream_headers_);
  if (active_request_) {
    conn_log_trace("body size={}", connection_, length);
    Buffer::OwnedImpl buffer(data, length);
    active_request_->request_decoder_->decodeData(buffer, false);
  }
}

void ServerConnectionImpl::onMessageComplete() {
  if (active_request_) {
    conn_log_trace("message complete", connection_);
    Buffer::OwnedImpl buffer;
    active_request_->remote_complete_ = true;

    if (deferred_end_stream_headers_) {
      active_request_->request_decoder_->decodeHeaders(std::move(deferred_end_stream_headers_),
                                                       true);
      deferred_end_stream_headers_.reset();
    } else {
      active_request_->request_decoder_->decodeData(buffer, true);
    }
  }

  // Always pause the parser so that the calling code can process 1 request at a time and apply
  // back pressure. However this means that the calling code needs to detect if there is more data
  // in the buffer and dispatch it again.
  http_parser_pause(&parser_, 1);
}

void ServerConnectionImpl::onResetStream(StreamResetReason reason) {
  ASSERT(active_request_);
  active_request_->response_encoder_.runResetCallbacks(reason);
  active_request_.reset();
}

void ServerConnectionImpl::sendProtocolError() {
  // We do this here because we may get a protocol error before we have a logical stream. Higher
  // layers can only operate on streams, so there is no coherent way to allow them to send a 400
  // "out of band." On one hand this is kind of a hack but on the other hand it normalizes HTTP/1.1
  // to look more like HTTP/2 to higher layers.
  if (!active_request_ || !active_request_->response_encoder_.startedResponse()) {
    Buffer::OwnedImpl bad_request_response("HTTP/1.1 400 Bad Request\r\n"
                                           "content-length: 0\r\n"
                                           "connection: close\r\n\r\n");

    connection_.write(bad_request_response);
  }
}

ClientConnectionImpl::ClientConnectionImpl(Network::Connection& connection, ConnectionCallbacks&)
    : ConnectionImpl(connection, HTTP_RESPONSE) {}

bool ClientConnectionImpl::cannotHaveBody() {
  if ((!pending_responses_.empty() && pending_responses_.front().head_request_) ||
      parser_.status_code == 204 || parser_.status_code == 304) {
    return true;
  } else {
    return false;
  }
}

StreamEncoder& ClientConnectionImpl::newStream(StreamDecoder& response_decoder) {
  if (resetStreamCalled()) {
    throw CodecClientException("cannot create new streams after calling reset");
  }

  request_encoder_.reset(new RequestStreamEncoderImpl(*this));
  pending_responses_.emplace_back(&response_decoder);
  return *request_encoder_;
}

void ClientConnectionImpl::onEncodeComplete() {
  // Transfer head request state into the pending response before we reuse the encoder.
  pending_responses_.back().head_request_ = request_encoder_->headRequest();
}

int ClientConnectionImpl::onHeadersComplete(HeaderMapPtr&& headers) {
  headers->addViaMoveValue(Headers::get().Status, std::to_string(parser_.status_code));

  // Handle the case where the client is closing a kept alive connection (by sending a 408
  // with a 'Connection: close' header). In this case we just let response flush out followed
  // by the remote close.
  if (pending_responses_.empty() && !resetStreamCalled()) {
    throw PrematureResponseException(std::move(headers));
  } else if (!pending_responses_.empty()) {
    if (cannotHaveBody()) {
      deferred_end_stream_headers_ = std::move(headers);
    } else {
      pending_responses_.front().decoder_->decodeHeaders(std::move(headers), false);
    }
  }

  // Here we deal with cases where the response cannot have a body, but http_parser does not deal
  // with it for us.
  return cannotHaveBody() ? 1 : 0;
}

void ClientConnectionImpl::onBody(const char* data, size_t length) {
  ASSERT(!deferred_end_stream_headers_);
  if (!pending_responses_.empty()) {
    Buffer::OwnedImpl buffer;
    buffer.add(data, length);
    pending_responses_.front().decoder_->decodeData(buffer, false);
  }
}

void ClientConnectionImpl::onMessageComplete() {
  if (!pending_responses_.empty()) {
    // After calling decodeData() with end stream set to true, we should no longer be able to reset.
    PendingResponse response = pending_responses_.front();
    pending_responses_.pop_front();

    if (deferred_end_stream_headers_) {
      response.decoder_->decodeHeaders(std::move(deferred_end_stream_headers_), true);
      deferred_end_stream_headers_.reset();
    } else {
      Buffer::OwnedImpl buffer;
      response.decoder_->decodeData(buffer, true);
    }
  }
}

void ClientConnectionImpl::onResetStream(StreamResetReason reason) {
  // Only raise reset if we did not already dispatch a complete response.
  if (!pending_responses_.empty()) {
    pending_responses_.clear();
    request_encoder_->runResetCallbacks(reason);
  }
}

} // Http1
} // Http
