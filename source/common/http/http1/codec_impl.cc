#include "common/http/http1/codec_impl.h"

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"

#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/stack_array.h"
#include "common/common/utility.h"
#include "common/http/exception.h"
#include "common/http/header_utility.h"
#include "common/http/headers.h"
#include "common/http/http1/header_formatter.h"
#include "common/http/utility.h"
#include "common/runtime/runtime_impl.h"

namespace Envoy {
namespace Http {
namespace Http1 {
namespace {

const StringUtil::CaseUnorderedSet& caseUnorderdSetContainingUpgradeAndHttp2Settings() {
  CONSTRUCT_ON_FIRST_USE(StringUtil::CaseUnorderedSet,
                         Http::Headers::get().ConnectionValues.Upgrade,
                         Http::Headers::get().ConnectionValues.Http2Settings);
}

} // namespace

const std::string StreamEncoderImpl::CRLF = "\r\n";
const std::string StreamEncoderImpl::LAST_CHUNK = "0\r\n\r\n";

StreamEncoderImpl::StreamEncoderImpl(ConnectionImpl& connection,
                                     HeaderKeyFormatter* header_key_formatter)
    : connection_(connection), header_key_formatter_(header_key_formatter) {
  if (connection_.connection().aboveHighWatermark()) {
    runHighWatermarkCallbacks();
  }
}

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
void StreamEncoderImpl::encodeHeader(absl::string_view key, absl::string_view value) {
  this->encodeHeader(key.data(), key.size(), value.data(), value.size());
}

void StreamEncoderImpl::encodeFormattedHeader(absl::string_view key, absl::string_view value) {
  if (header_key_formatter_ != nullptr) {
    encodeHeader(header_key_formatter_->format(key), value);
  } else {
    encodeHeader(key, value);
  }
}

void StreamEncoderImpl::encode100ContinueHeaders(const HeaderMap& headers) {
  ASSERT(headers.Status()->value() == "100");
  processing_100_continue_ = true;
  encodeHeaders(headers, false);
  processing_100_continue_ = false;
}

void StreamEncoderImpl::encodeHeaders(const HeaderMap& headers, bool end_stream) {
  bool saw_content_length = false;
  headers.iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        absl::string_view key_to_use = header.key().getStringView();
        uint32_t key_size_to_use = header.key().size();
        // Translate :authority -> host so that upper layers do not need to deal with this.
        if (key_size_to_use > 1 && key_to_use[0] == ':' && key_to_use[1] == 'a') {
          key_to_use = absl::string_view(Headers::get().HostLegacy.get());
          key_size_to_use = Headers::get().HostLegacy.get().size();
        }

        // Skip all headers starting with ':' that make it here.
        if (key_to_use[0] == ':') {
          return HeaderMap::Iterate::Continue;
        }

        static_cast<StreamEncoderImpl*>(context)->encodeFormattedHeader(
            key_to_use, header.value().getStringView());

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
  // for streaming (e.g. SSE stream sent to hystrix dashboard), we do not want
  // chunk transfer encoding but we don't have a content-length so we pass "envoy only"
  // header to avoid adding chunks
  //
  // Note that for HEAD requests Envoy does best-effort guessing when there is no
  // content-length. If a client makes a HEAD request for an upstream resource
  // with no bytes but the upstream response doesn't include "Content-length: 0",
  // Envoy will incorrectly assume a subsequent response to GET will be chunk encoded.
  if (saw_content_length || headers.NoChunks()) {
    chunk_encoding_ = false;
  } else {
    if (processing_100_continue_) {
      // Make sure we don't serialize chunk information with 100-Continue headers.
      chunk_encoding_ = false;
    } else if (end_stream && !is_response_to_head_request_) {
      // If this is a headers-only stream, append an explicit "Content-Length: 0" unless it's a
      // response to a HEAD request.
      // For 204s and 1xx where content length is disallowed, don't append the content length but
      // also don't chunk encode.
      if (is_content_length_allowed_) {
        encodeFormattedHeader(Headers::get().ContentLength.get(), "0");
      }
      chunk_encoding_ = false;
    } else if (connection_.protocol() == Protocol::Http10) {
      chunk_encoding_ = false;
    } else {
      encodeFormattedHeader(Headers::get().TransferEncoding.get(),
                            Headers::get().TransferEncodingValues.Chunked);
      // We do not apply chunk encoding for HTTP upgrades.
      // If there is a body in a WebSocket Upgrade response, the chunks will be
      // passed through via maybeDirectDispatch so we need to avoid appending
      // extra chunk boundaries.
      //
      // When sending a response to a HEAD request Envoy may send an informational
      // "Transfer-Encoding: chunked" header, but should not send a chunk encoded body.
      chunk_encoding_ = !Utility::isUpgrade(headers) && !is_response_to_head_request_;
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
  // actually write the zero length buffer out.
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

void StreamEncoderImpl::encodeMetadata(const MetadataMapVector&) {
  connection_.stats().metadata_not_supported_error_.inc();
}

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

  connection().write(output_buffer_, false);
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
static const char HTTP_10_RESPONSE_PREFIX[] = "HTTP/1.0 ";

void ResponseStreamEncoderImpl::encodeHeaders(const HeaderMap& headers, bool end_stream) {
  started_response_ = true;
  uint64_t numeric_status = Utility::getResponseStatus(headers);

  connection_.reserveBuffer(4096);
  if (connection_.protocol() == Protocol::Http10 && connection_.supports_http_10()) {
    connection_.copyToBuffer(HTTP_10_RESPONSE_PREFIX, sizeof(HTTP_10_RESPONSE_PREFIX) - 1);
  } else {
    connection_.copyToBuffer(RESPONSE_PREFIX, sizeof(RESPONSE_PREFIX) - 1);
  }
  connection_.addIntToBuffer(numeric_status);
  connection_.addCharToBuffer(' ');

  const char* status_string = CodeUtility::toString(static_cast<Code>(numeric_status));
  uint32_t status_string_len = strlen(status_string);
  connection_.copyToBuffer(status_string, status_string_len);

  connection_.addCharToBuffer('\r');
  connection_.addCharToBuffer('\n');

  if (numeric_status == 204 || numeric_status < 200) {
    // Per https://tools.ietf.org/html/rfc7230#section-3.3.2
    setIsContentLengthAllowed(false);
  } else {
    // Make sure that if we encodeHeaders(100) then encodeHeaders(200) that we
    // set is_content_length_allowed_ back to true.
    setIsContentLengthAllowed(true);
  }

  StreamEncoderImpl::encodeHeaders(headers, end_stream);
}

static const char REQUEST_POSTFIX[] = " HTTP/1.1\r\n";

void RequestStreamEncoderImpl::encodeHeaders(const HeaderMap& headers, bool end_stream) {
  const HeaderEntry* method = headers.Method();
  const HeaderEntry* path = headers.Path();
  if (!method || !path) {
    throw CodecClientException(":method and :path must be specified");
  }
  if (method->value() == Headers::get().MethodValues.Head) {
    head_request_ = true;
  }
  connection_.onEncodeHeaders(headers);
  connection_.reserveBuffer(std::max(4096U, path->value().size() + 4096));
  connection_.copyToBuffer(method->value().getStringView().data(), method->value().size());
  connection_.addCharToBuffer(' ');
  connection_.copyToBuffer(path->value().getStringView().data(), path->value().size());
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
      static_cast<ConnectionImpl*>(parser->data)->onMessageCompleteBase();
      return 0;
    },
    nullptr, // on_chunk_header
    nullptr  // on_chunk_complete
};

const ToLowerTable& ConnectionImpl::toLowerTable() {
  static auto* table = new ToLowerTable();
  return *table;
}

ConnectionImpl::ConnectionImpl(Network::Connection& connection, Stats::Scope& stats,
                               http_parser_type type, uint32_t max_headers_kb,
                               const uint32_t max_headers_count)
    : connection_(connection), stats_{ALL_HTTP1_CODEC_STATS(POOL_COUNTER_PREFIX(stats, "http1."))},
      output_buffer_([&]() -> void { this->onBelowLowWatermark(); },
                     [&]() -> void { this->onAboveHighWatermark(); }),
      max_headers_kb_(max_headers_kb), max_headers_count_(max_headers_count),
      strict_header_validation_(
          Runtime::runtimeFeatureEnabled("envoy.reloadable_features.strict_header_validation")) {
  output_buffer_.setWatermarks(connection.bufferLimit());
  http_parser_init(&parser_, type);
  parser_.data = this;
}

void ConnectionImpl::completeLastHeader() {
  ENVOY_CONN_LOG(trace, "completed header: key={} value={}", connection_,
                 current_header_field_.getStringView(), current_header_value_.getStringView());
  if (!current_header_field_.empty()) {
    toLowerTable().toLowerCase(current_header_field_.buffer(), current_header_field_.size());
    current_header_map_->addViaMove(std::move(current_header_field_),
                                    std::move(current_header_value_));
  }
  // Check if the number of headers exceeds the limit.
  if (current_header_map_->size() > max_headers_count_) {
    error_code_ = Http::Code::RequestHeaderFieldsTooLarge;
    sendProtocolError();
    throw CodecProtocolException("headers size exceeds limit");
  }

  header_parsing_state_ = HeaderParsingState::Field;
  ASSERT(current_header_field_.empty());
  ASSERT(current_header_value_.empty());
}

bool ConnectionImpl::maybeDirectDispatch(Buffer::Instance& data) {
  if (!handling_upgrade_) {
    // Only direct dispatch for Upgrade requests.
    return false;
  }

  ssize_t total_parsed = 0;
  uint64_t num_slices = data.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
  data.getRawSlices(slices.begin(), num_slices);
  for (const Buffer::RawSlice& slice : slices) {
    total_parsed += slice.len_;
    onBody(static_cast<const char*>(slice.mem_), slice.len_);
  }
  ENVOY_CONN_LOG(trace, "direct-dispatched {} bytes", connection_, total_parsed);
  data.drain(total_parsed);
  return true;
}

void ConnectionImpl::dispatch(Buffer::Instance& data) {
  ENVOY_CONN_LOG(trace, "parsing {} bytes", connection_, data.length());

  if (maybeDirectDispatch(data)) {
    return;
  }

  // Always unpause before dispatch.
  http_parser_pause(&parser_, 0);

  ssize_t total_parsed = 0;
  if (data.length() > 0) {
    uint64_t num_slices = data.getRawSlices(nullptr, 0);
    STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
    data.getRawSlices(slices.begin(), num_slices);
    for (const Buffer::RawSlice& slice : slices) {
      total_parsed += dispatchSlice(static_cast<const char*>(slice.mem_), slice.len_);
    }
  } else {
    dispatchSlice(nullptr, 0);
  }

  ENVOY_CONN_LOG(trace, "parsed {} bytes", connection_, total_parsed);
  data.drain(total_parsed);

  // If an upgrade has been handled and there is body data or early upgrade
  // payload to send on, send it on.
  maybeDirectDispatch(data);
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

  const absl::string_view header_value = absl::string_view(data, length);

  if (strict_header_validation_) {
    if (!Http::HeaderUtility::headerIsValid(header_value)) {
      ENVOY_CONN_LOG(debug, "invalid header value: {}", connection_, header_value);
      error_code_ = Http::Code::BadRequest;
      sendProtocolError();
      throw CodecProtocolException("http/1.1 protocol error: header value contains invalid chars");
    }
  } else if (header_value.find('\0') != absl::string_view::npos) {
    // http-parser should filter for this
    // (https://tools.ietf.org/html/rfc7230#section-3.2.6), but it doesn't today. HeaderStrings
    // have an invariant that they must not contain embedded zero characters
    // (NUL, ASCII 0x0).
    throw CodecProtocolException("http/1.1 protocol error: header value contains NUL");
  }

  header_parsing_state_ = HeaderParsingState::Value;
  current_header_value_.append(data, length);

  // Verify that the cached value in byte size exists.
  ASSERT(current_header_map_->byteSize().has_value());
  const uint32_t total = current_header_field_.size() + current_header_value_.size() +
                         current_header_map_->byteSize().value();
  if (total > (max_headers_kb_ * 1024)) {

    error_code_ = Http::Code::RequestHeaderFieldsTooLarge;
    sendProtocolError();
    throw CodecProtocolException("headers size exceeds limit");
  }
}

int ConnectionImpl::onHeadersCompleteBase() {
  ENVOY_CONN_LOG(trace, "headers complete", connection_);
  completeLastHeader();
  // Validate that the completed HeaderMap's cached byte size exists and is correct.
  // This assert iterates over the HeaderMap.
  ASSERT(current_header_map_->byteSize().has_value() &&
         current_header_map_->byteSize() == current_header_map_->byteSizeInternal());
  if (!(parser_.http_major == 1 && parser_.http_minor == 1)) {
    // This is not necessarily true, but it's good enough since higher layers only care if this is
    // HTTP/1.1 or not.
    protocol_ = Protocol::Http10;
  }
  if (Utility::isUpgrade(*current_header_map_)) {
    // Ignore h2c upgrade requests until we support them.
    // See https://github.com/envoyproxy/envoy/issues/7161 for details.
    if (current_header_map_->Upgrade() &&
        absl::EqualsIgnoreCase(current_header_map_->Upgrade()->value().getStringView(),
                               Http::Headers::get().UpgradeValues.H2c)) {
      ENVOY_CONN_LOG(trace, "removing unsupported h2c upgrade headers.", connection_);
      current_header_map_->removeUpgrade();
      if (current_header_map_->Connection()) {
        const auto& tokens_to_remove = caseUnorderdSetContainingUpgradeAndHttp2Settings();
        std::string new_value = StringUtil::removeTokens(
            current_header_map_->Connection()->value().getStringView(), ",", tokens_to_remove, ",");
        if (new_value.empty()) {
          current_header_map_->removeConnection();
        } else {
          current_header_map_->Connection()->value(new_value);
        }
      }
      current_header_map_->remove(Headers::get().Http2Settings);
    } else {
      ENVOY_CONN_LOG(trace, "codec entering upgrade mode.", connection_);
      handling_upgrade_ = true;
    }
  }

  int rc = onHeadersComplete(std::move(current_header_map_));
  current_header_map_.reset();
  header_parsing_state_ = HeaderParsingState::Done;

  // Returning 2 informs http_parser to not expect a body or further data on this connection.
  return handling_upgrade_ ? 2 : rc;
}

void ConnectionImpl::onMessageCompleteBase() {
  ENVOY_CONN_LOG(trace, "message complete", connection_);
  if (handling_upgrade_) {
    // If this is an upgrade request, swallow the onMessageComplete. The
    // upgrade payload will be treated as stream body.
    ASSERT(!deferred_end_stream_headers_);
    ENVOY_CONN_LOG(trace, "Pausing parser due to upgrade.", connection_);
    http_parser_pause(&parser_, 1);
    return;
  }
  onMessageComplete();
}

void ConnectionImpl::onMessageBeginBase() {
  ENVOY_CONN_LOG(trace, "message begin", connection_);
  // Make sure that if HTTP/1.0 and HTTP/1.1 requests share a connection Envoy correctly sets
  // protocol for each request. Envoy defaults to 1.1 but sets the protocol to 1.0 where applicable
  // in onHeadersCompleteBase
  protocol_ = Protocol::Http11;
  ASSERT(!current_header_map_);
  current_header_map_ = std::make_unique<HeaderMapImpl>();
  header_parsing_state_ = HeaderParsingState::Field;
  onMessageBegin();
}

void ConnectionImpl::onResetStreamBase(StreamResetReason reason) {
  ASSERT(!reset_stream_called_);
  reset_stream_called_ = true;
  onResetStream(reason);
}

ServerConnectionImpl::ServerConnectionImpl(Network::Connection& connection, Stats::Scope& stats,
                                           ServerConnectionCallbacks& callbacks,
                                           Http1Settings settings, uint32_t max_request_headers_kb,
                                           const uint32_t max_request_headers_count)
    : ConnectionImpl(connection, stats, HTTP_REQUEST, max_request_headers_kb,
                     max_request_headers_count),
      callbacks_(callbacks), codec_settings_(settings) {
  switch (codec_settings_.header_key_format_) {
  case Http1Settings::HeaderKeyFormat::Default:
    break;
  case Http1Settings::HeaderKeyFormat::ProperCase:
    header_key_formatter_ = std::make_unique<ProperCaseHeaderKeyFormatter>();
    break;
  }
}

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
  if (!active_request_->request_url_.getStringView().empty() &&
      (active_request_->request_url_.getStringView()[0] == '/' ||
       ((method == HTTP_OPTIONS) && active_request_->request_url_.getStringView()[0] == '*'))) {
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

  Utility::Url absolute_url;
  if (!absolute_url.initialize(active_request_->request_url_.getStringView())) {
    sendProtocolError();
    throw CodecProtocolException("http/1.1 protocol error: invalid url in request line");
  }
  // RFC7230#5.7
  // When a proxy receives a request with an absolute-form of
  // request-target, the proxy MUST ignore the received Host header field
  // (if any) and instead replace it with the host information of the
  // request-target. A proxy that forwards such a request MUST generate a
  // new Host field-value based on the received request-target rather than
  // forward the received Host field-value.
  headers.insertHost().value(std::string(absolute_url.host_and_port()));

  headers.insertPath().value(std::string(absolute_url.path_and_query_params()));
  active_request_->request_url_.clear();
}

int ServerConnectionImpl::onHeadersComplete(HeaderMapImplPtr&& headers) {
  // Handle the case where response happens prior to request complete. It's up to upper layer code
  // to disconnect the connection but we shouldn't fire any more events since it doesn't make
  // sense.
  if (active_request_) {
    const char* method_string = http_method_str(static_cast<http_method>(parser_.method));

    // Inform the response encoder about any HEAD method, so it can set content
    // length and transfer encoding headers correctly.
    active_request_->response_encoder_.isResponseToHeadRequest(parser_.method == HTTP_HEAD);

    // Currently, CONNECT is not supported, however; http_parser_parse_url needs to know about
    // CONNECT
    handlePath(*headers, parser_.method);
    ASSERT(active_request_->request_url_.empty());

    headers->insertMethod().value(method_string, strlen(method_string));

    // Determine here whether we have a body or not. This uses the new RFC semantics where the
    // presence of content-length or chunked transfer-encoding indicates a body vs. a particular
    // method. If there is no body, we defer raising decodeHeaders() until the parser is flushed
    // with message complete. This allows upper layers to behave like HTTP/2 and prevents a proxy
    // scenario where the higher layers stream through and implicitly switch to chunked transfer
    // encoding because end stream with zero body length has not yet been indicated.
    if (parser_.flags & F_CHUNKED ||
        (parser_.content_length > 0 && parser_.content_length != ULLONG_MAX) || handling_upgrade_) {
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
    active_request_ = std::make_unique<ActiveRequest>(*this, header_key_formatter_.get());
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

    connection_.write(bad_request_response, false);
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

ClientConnectionImpl::ClientConnectionImpl(Network::Connection& connection, Stats::Scope& stats,
                                           ConnectionCallbacks&,
                                           const uint32_t max_response_headers_count)
    : ConnectionImpl(connection, stats, HTTP_RESPONSE, MAX_RESPONSE_HEADERS_KB,
                     max_response_headers_count) {}

bool ClientConnectionImpl::cannotHaveBody() {
  if ((!pending_responses_.empty() && pending_responses_.front().head_request_) ||
      parser_.status_code == 204 || parser_.status_code == 304 ||
      (parser_.status_code >= 200 && parser_.content_length == 0)) {
    return true;
  } else {
    return false;
  }
}

StreamEncoder& ClientConnectionImpl::newStream(StreamDecoder& response_decoder) {
  if (resetStreamCalled()) {
    throw CodecClientException("cannot create new streams after calling reset");
  }

  // If reads were disabled due to flow control, we expect reads to always be enabled again before
  // reusing this connection. This is done when the final pipeline response is received.
  ASSERT(connection_.readEnabled());

  request_encoder_ = std::make_unique<RequestStreamEncoderImpl>(*this);
  pending_responses_.emplace_back(&response_decoder);
  return *request_encoder_;
}

void ClientConnectionImpl::onEncodeHeaders(const HeaderMap& headers) {
  if (headers.Method()->value() == Headers::get().MethodValues.Head.c_str()) {
    pending_responses_.back().head_request_ = true;
  }
}

int ClientConnectionImpl::onHeadersComplete(HeaderMapImplPtr&& headers) {
  headers->insertStatus().value(parser_.status_code);

  // Handle the case where the client is closing a kept alive connection (by sending a 408
  // with a 'Connection: close' header). In this case we just let response flush out followed
  // by the remote close.
  if (pending_responses_.empty() && !resetStreamCalled()) {
    throw PrematureResponseException(std::move(headers));
  } else if (!pending_responses_.empty()) {
    if (parser_.status_code == 100) {
      // http-parser treats 100 continue headers as their own complete response.
      // Swallow the spurious onMessageComplete and continue processing.
      ignore_message_complete_for_100_continue_ = true;
      pending_responses_.front().decoder_->decode100ContinueHeaders(std::move(headers));
    } else if (cannotHaveBody()) {
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
  ENVOY_CONN_LOG(trace, "message complete", connection_);
  if (ignore_message_complete_for_100_continue_) {
    ignore_message_complete_for_100_continue_ = false;
    return;
  }
  if (!pending_responses_.empty()) {
    // After calling decodeData() with end stream set to true, we should no longer be able to reset.
    PendingResponse response = pending_responses_.front();
    pending_responses_.pop_front();

    // Streams are responsible for unwinding any outstanding readDisable(true)
    // calls done on the underlying connection as they are destroyed. As this is
    // the only place a HTTP/1 stream is destroyed where the Network::Connection is
    // reused, unwind any outstanding readDisable() calls here. Only do this if there are no
    // pipelined responses remaining. Also do this before we dispatch end_stream in case the caller
    // immediately reuses the connection.
    if (pending_responses_.empty()) {
      while (!connection_.readEnabled()) {
        connection_.readDisable(false);
      }
    }

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

void ClientConnectionImpl::onAboveHighWatermark() {
  // This should never happen without an active stream/request.
  ASSERT(!pending_responses_.empty());
  request_encoder_->runHighWatermarkCallbacks();
}

void ClientConnectionImpl::onBelowLowWatermark() {
  // This can get called without an active stream/request when upstream decides to do bad things
  // such as sending multiple responses to the same request, causing us to close the connection, but
  // in doing so go below low watermark.
  if (!pending_responses_.empty()) {
    request_encoder_->runLowWatermarkCallbacks();
  }
}

} // namespace Http1
} // namespace Http
} // namespace Envoy
