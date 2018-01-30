#include "common/http/http1/codec_impl.h"

#include <cstdint>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"

#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/http/exception.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Http {
namespace Http1 {

const std::string StreamEncoderImpl::CRLF = "\r\n";
const std::string StreamEncoderImpl::LAST_CHUNK = "0\r\n\r\n";

void StreamEncoderImpl::encodeHeader(const char* key, uint32_t key_size, const char* value,
                                     uint32_t value_size) {

  connection_.reserveBuffer(key_size + value_size + 4);
  ASSERT(key_size > 0);

  connection_.copyToBuffer(key, key_size);
  connection_.addCharToBuffer(':');
  connection_.addCharToBuffer(' ');
  connection_.copyToBuffer(value, value_size);
  connection_.addCharToBuffer('\r');
  connection_.addCharToBuffer('\n');
}

void StreamEncoderImpl::encodeHeaders(const HeaderMap& headers, bool end_stream) {
  bool saw_content_length = false;
  headers.iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        const char* key_to_use = header.key().c_str();
        uint32_t key_size_to_use = header.key().size();
        // Translate :authority -> host so that upper layers do not need to deal with this.
        if (key_size_to_use > 1 && key_to_use[0] == ':' && key_to_use[1] == 'a') {
          key_to_use = Headers::get().HostLegacy.get().c_str();
          key_size_to_use = Headers::get().HostLegacy.get().size();
        }

        // Skip all headers starting with ':' that make it here.
        if (key_to_use[0] == ':') {
          return HeaderMap::Iterate::Continue;
        }

        static_cast<StreamEncoderImpl*>(context)->encodeHeader(
            key_to_use, key_size_to_use, header.value().c_str(), header.value().size());
        return HeaderMap::Iterate::Continue;
      },
      this);

  if (headers.ContentLength()) {
    saw_content_length = true;
  }

  ASSERT(!headers.TransferEncoding());

  // Assume we are chunk encoding unless we are passed a content length or this is a header only
  // response. Upper layers generally should strip transfer-encoding since it only applies to
  // HTTP/1.1. The codec will infer it based on the type of response.
  if (saw_content_length) {
    chunk_encoding_ = false;
  } else {
    if (end_stream) {
      encodeHeader(Headers::get().ContentLength.get().c_str(),
                   Headers::get().ContentLength.get().size(), "0", 1);
      chunk_encoding_ = false;
    } else {
      encodeHeader(Headers::get().TransferEncoding.get().c_str(),
                   Headers::get().TransferEncoding.get().size(),
                   Headers::get().TransferEncodingValues.Chunked.c_str(),
                   Headers::get().TransferEncodingValues.Chunked.size());
      chunk_encoding_ = true;
    }
  }

  connection_.reserveBuffer(2);
  connection_.addCharToBuffer('\r');
  connection_.addCharToBuffer('\n');

  if (end_stream) {
    endEncode();
  } else {
    connection_.flushOutput();
  }
}

void StreamEncoderImpl::encodeData(Buffer::Instance& data, bool end_stream) {
  // end_stream may be indicated with a zero length data buffer. If that is the case, so not
  // atually write the zero length buffer out.
  if (data.length() > 0) {
    if (chunk_encoding_) {
      connection_.buffer().add(fmt::format("{:x}\r\n", data.length()));
    }

    connection_.buffer().move(data);

    if (chunk_encoding_) {
      connection_.buffer().add(CRLF);
    }
  }

  if (end_stream) {
    endEncode();
  } else {
    connection_.flushOutput();
  }
}

void StreamEncoderImpl::encodeTrailers(const HeaderMap&) { endEncode(); }

void StreamEncoderImpl::endEncode() {
  if (chunk_encoding_) {
    connection_.buffer().add(LAST_CHUNK);
  }

  connection_.flushOutput();
  connection_.onEncodeComplete();
}

void ConnectionImpl::flushOutput() {
  if (reserved_current_) {
    reserved_iovec_.len_ = reserved_current_ - static_cast<char*>(reserved_iovec_.mem_);
    output_buffer_.commit(&reserved_iovec_, 1);
    reserved_current_ = nullptr;
  }

  connection().write(output_buffer_);
  ASSERT(0UL == output_buffer_.length());
}

void ConnectionImpl::addCharToBuffer(char c) {
  ASSERT(bufferRemainingSize() >= 1);
  *reserved_current_++ = c;
}

void ConnectionImpl::addIntToBuffer(uint64_t i) {
  reserved_current_ += StringUtil::itoa(reserved_current_, bufferRemainingSize(), i);
}

uint64_t ConnectionImpl::bufferRemainingSize() {
  return reserved_iovec_.len_ - (reserved_current_ - static_cast<char*>(reserved_iovec_.mem_));
}

void ConnectionImpl::copyToBuffer(const char* data, uint64_t length) {
  ASSERT(bufferRemainingSize() >= length);
  memcpy(reserved_current_, data, length);
  reserved_current_ += length;
}

void ConnectionImpl::reserveBuffer(uint64_t size) {
  if (reserved_current_ && bufferRemainingSize() >= size) {
    return;
  }

  if (reserved_current_) {
    reserved_iovec_.len_ = reserved_current_ - static_cast<char*>(reserved_iovec_.mem_);
    output_buffer_.commit(&reserved_iovec_, 1);
  }

  // TODO PERF: It would be better to allow a split reservation. That will make fill code more
  //            complicated.
  output_buffer_.reserve(std::max<uint64_t>(4096, size), &reserved_iovec_, 1);
  reserved_current_ = static_cast<char*>(reserved_iovec_.mem_);
}

void StreamEncoderImpl::resetStream(StreamResetReason reason) {
  connection_.onResetStreamBase(reason);
}

void StreamEncoderImpl::readDisable(bool disable) { connection_.readDisable(disable); }

uint32_t StreamEncoderImpl::bufferLimit() { return connection_.bufferLimit(); }

static const char RESPONSE_PREFIX[] = "HTTP/1.1 ";

void ResponseStreamEncoderImpl::encodeHeaders(const HeaderMap& headers, bool end_stream) {
  started_response_ = true;
  uint64_t numeric_status = Utility::getResponseStatus(headers);

  connection_.reserveBuffer(4096);
  connection_.copyToBuffer(RESPONSE_PREFIX, sizeof(RESPONSE_PREFIX) - 1);
  connection_.addIntToBuffer(numeric_status);
  connection_.addCharToBuffer(' ');

  const char* status_string = CodeUtility::toString(static_cast<Code>(numeric_status));
  uint32_t status_string_len = strlen(status_string);
  connection_.copyToBuffer(status_string, status_string_len);

  connection_.addCharToBuffer('\r');
  connection_.addCharToBuffer('\n');

  StreamEncoderImpl::encodeHeaders(headers, end_stream);
}

static const char REQUEST_POSTFIX[] = " HTTP/1.1\r\n";

void RequestStreamEncoderImpl::encodeHeaders(const HeaderMap& headers, bool end_stream) {
  const HeaderEntry* method = headers.Method();
  const HeaderEntry* path = headers.Path();
  if (!method || !path) {
    throw CodecClientException(":method and :path must be specified");
  }

  if (method->value() == Headers::get().MethodValues.Head.c_str()) {
    head_request_ = true;
  }

  connection_.reserveBuffer(std::max(4096U, path->value().size() + 4096));
  connection_.copyToBuffer(method->value().c_str(), method->value().size());
  connection_.addCharToBuffer(' ');
  connection_.copyToBuffer(path->value().c_str(), path->value().size());
  connection_.copyToBuffer(REQUEST_POSTFIX, sizeof(REQUEST_POSTFIX) - 1);

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
    [](http_parser* parser) -> int {
      return static_cast<ConnectionImpl*>(parser->data)->onHeadersCompleteBase();
    },
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

const ToLowerTable& ConnectionImpl::toLowerTable() {
  static ToLowerTable* table = new ToLowerTable();
  return *table;
}

ConnectionImpl::ConnectionImpl(Network::Connection& connection, http_parser_type type)
    : connection_(connection), output_buffer_([&]() -> void { this->onBelowLowWatermark(); },
                                              [&]() -> void { this->onAboveHighWatermark(); }) {
  output_buffer_.setWatermarks(connection.bufferLimit());
  http_parser_init(&parser_, type);
  parser_.data = this;
}

void ConnectionImpl::completeLastHeader() {
  ENVOY_CONN_LOG(trace, "completed header: key={} value={}", connection_,
                 current_header_field_.c_str(), current_header_value_.c_str());
  if (!current_header_field_.empty()) {
    toLowerTable().toLowerCase(current_header_field_.buffer(), current_header_field_.size());
    current_header_map_->addViaMove(std::move(current_header_field_),
                                    std::move(current_header_value_));
  }

  header_parsing_state_ = HeaderParsingState::Field;
  ASSERT(current_header_field_.empty());
  ASSERT(current_header_value_.empty());
}

void ConnectionImpl::dispatch(Buffer::Instance& data) {
  ENVOY_CONN_LOG(trace, "parsing {} bytes", connection_, data.length());

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

  ENVOY_CONN_LOG(trace, "parsed {} bytes", connection_, total_parsed);
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
  ENVOY_CONN_LOG(trace, "headers complete", connection_);
  completeLastHeader();
  if (!(parser_.http_major == 1 && parser_.http_minor == 1)) {
    // This is not necessarily true, but it's good enough since higher layers only care if this is
    // HTTP/1.1 or not.
    protocol_ = Protocol::Http10;
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
                                           ServerConnectionCallbacks& callbacks,
                                           Http1Settings settings)
    : ConnectionImpl(connection, HTTP_REQUEST), callbacks_(callbacks), codec_settings_(settings) {}

void ServerConnectionImpl::onEncodeComplete() {
  ASSERT(active_request_);
  if (active_request_->remote_complete_) {
    // Only do this if remote is complete. If we are replying before the request is complete the
    // only logical thing to do is for higher level code to reset() / close the connection so we
    // leave the request around so that it can fire reset callbacks.
    active_request_.reset();
  }
}

void ServerConnectionImpl::handlePath(HeaderMapImpl& headers, unsigned int method) {
  HeaderString path(Headers::get().Path);

  bool is_connect = (method == HTTP_CONNECT);

  // The url is relative or a wildcard when the method is OPTIONS. Nothing to do here.
  if (active_request_->request_url_.c_str()[0] == '/' ||
      ((method == HTTP_OPTIONS) && active_request_->request_url_.c_str()[0] == '*')) {
    headers.addViaMove(std::move(path), std::move(active_request_->request_url_));
    return;
  }

  // If absolute_urls and/or connect are not going be handled, copy the url and return.
  // This forces the behavior to be backwards compatible with the old codec behavior.
  if (!codec_settings_.allow_absolute_url_) {
    headers.addViaMove(std::move(path), std::move(active_request_->request_url_));
    return;
  }

  if (is_connect) {
    headers.addViaMove(std::move(path), std::move(active_request_->request_url_));
    return;
  }

  struct http_parser_url u;
  http_parser_url_init(&u);
  int result = http_parser_parse_url(active_request_->request_url_.buffer(),
                                     active_request_->request_url_.size(), is_connect, &u);

  if (result != 0) {
    sendProtocolError();
    throw CodecProtocolException(
        "http/1.1 protocol error: invalid url in request line, parsed invalid");
  } else {
    if ((u.field_set & (1 << UF_HOST)) == (1 << UF_HOST) &&
        (u.field_set & (1 << UF_SCHEMA)) == (1 << UF_SCHEMA)) {
      // RFC7230#5.7
      // When a proxy receives a request with an absolute-form of
      // request-target, the proxy MUST ignore the received Host header field
      // (if any) and instead replace it with the host information of the
      // request-target. A proxy that forwards such a request MUST generate a
      // new Host field-value based on the received request-target rather than
      // forward the received Host field-value.

      uint16_t authority_len = u.field_data[UF_HOST].len;

      if ((u.field_set & (1 << UF_PORT)) == (1 << UF_PORT)) {
        authority_len = authority_len + u.field_data[UF_PORT].len + 1;
      }

      // Insert the host header, this will later be converted to :authority
      std::string new_host(active_request_->request_url_.c_str() + u.field_data[UF_HOST].off,
                           authority_len);

      headers.insertHost().value(new_host);

      // RFC allows the absolute-uri to not end in /, but the absolute path form
      // must start with /
      if ((u.field_set & (1 << UF_PATH)) == (1 << UF_PATH) && u.field_data[UF_PATH].len > 0) {
        HeaderString new_path;
        new_path.setCopy(active_request_->request_url_.c_str() + u.field_data[UF_PATH].off,
                         active_request_->request_url_.size() - u.field_data[UF_PATH].off);
        headers.addViaMove(std::move(path), std::move(new_path));
      } else {
        HeaderString new_path;
        new_path.setCopy("/", 1);
        headers.addViaMove(std::move(path), std::move(new_path));
      }

      active_request_->request_url_.clear();
      return;
    }
    sendProtocolError();
    throw CodecProtocolException("http/1.1 protocol error: invalid url in request line");
  }
}

int ServerConnectionImpl::onHeadersComplete(HeaderMapImplPtr&& headers) {
  // Handle the case where response happens prior to request complete. It's up to upper layer code
  // to disconnect the connection but we shouldn't fire any more events since it doesn't make
  // sense.
  if (active_request_) {
    const char* method_string = http_method_str(static_cast<http_method>(parser_.method));

    // Currently, CONNECT is not supported, however; http_parser_parse_url needs to know about
    // CONNECT
    handlePath(*headers, parser_.method);
    ASSERT(active_request_->request_url_.empty());

    headers->insertMethod().value(method_string, strlen(method_string));

    // Deal with expect: 100-continue here since higher layers are never going to do anything other
    // than say to continue so that we can respond before request complete if necessary.
    if (headers->Expect() &&
        0 == StringUtil::caseInsensitiveCompare(headers->Expect()->value().c_str(),
                                                Headers::get().ExpectValues._100Continue.c_str())) {
      Buffer::OwnedImpl continue_response("HTTP/1.1 100 Continue\r\n\r\n");
      connection_.write(continue_response);
      headers->removeExpect();
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
    ENVOY_CONN_LOG(trace, "body size={}", connection_, length);
    Buffer::OwnedImpl buffer(data, length);
    active_request_->request_decoder_->decodeData(buffer, false);
  }
}

void ServerConnectionImpl::onMessageComplete() {
  if (active_request_) {
    ENVOY_CONN_LOG(trace, "message complete", connection_);
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
  // layers can only operate on streams, so there is no coherent way to allow them to send an error
  // "out of band." On one hand this is kind of a hack but on the other hand it normalizes HTTP/1.1
  // to look more like HTTP/2 to higher layers.
  if (!active_request_ || !active_request_->response_encoder_.startedResponse()) {
    Buffer::OwnedImpl bad_request_response(
        fmt::format("HTTP/1.1 {} {}\r\ncontent-length: 0\r\nconnection: close\r\n\r\n",
                    std::to_string(enumToInt(error_code_)), CodeUtility::toString(error_code_)));

    connection_.write(bad_request_response);
  }
}

void ServerConnectionImpl::onAboveHighWatermark() {
  if (active_request_) {
    active_request_->response_encoder_.runHighWatermarkCallbacks();
  }
}
void ServerConnectionImpl::onBelowLowWatermark() {
  if (active_request_) {
    active_request_->response_encoder_.runLowWatermarkCallbacks();
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
  // Streams are responsible for unwinding any outstanding readDisable(true)
  // calls done on the underlying connection as they are destroyed. As this is
  // the only place a HTTP/1 stream is destroyed where the Network::Connection is
  // reused, unwind any outstanding readDisable() calls here.
  while (!connection_.readEnabled()) {
    connection_.readDisable(false);
  }
  request_encoder_.reset(new RequestStreamEncoderImpl(*this));
  pending_responses_.emplace_back(&response_decoder);
  return *request_encoder_;
}

void ClientConnectionImpl::onEncodeComplete() {
  // Transfer head request state into the pending response before we reuse the encoder.
  pending_responses_.back().head_request_ = request_encoder_->headRequest();
}

int ClientConnectionImpl::onHeadersComplete(HeaderMapImplPtr&& headers) {
  headers->insertStatus().value(parser_.status_code);

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

void ClientConnectionImpl::onAboveHighWatermark() { request_encoder_->runHighWatermarkCallbacks(); }

void ClientConnectionImpl::onBelowLowWatermark() { request_encoder_->runLowWatermarkCallbacks(); }

} // namespace Http1
} // namespace Http
} // namespace Envoy
