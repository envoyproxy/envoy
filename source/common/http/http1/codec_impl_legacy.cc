#include "common/http/http1/codec_impl_legacy.h"

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"

#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/http/exception.h"
#include "common/http/header_utility.h"
#include "common/http/headers.h"
#include "common/http/http1/header_formatter.h"
#include "common/http/url_utility.h"
#include "common/http/utility.h"
#include "common/runtime/runtime_features.h"

#include "absl/container/fixed_array.h"
#include "absl/strings/ascii.h"

namespace Envoy {
namespace Http {
namespace Legacy {
namespace Http1 {
namespace {

struct Http1ResponseCodeDetailValues {
  const absl::string_view TooManyHeaders = "http1.too_many_headers";
  const absl::string_view HeadersTooLarge = "http1.headers_too_large";
  const absl::string_view HttpCodecError = "http1.codec_error";
  const absl::string_view InvalidCharacters = "http1.invalid_characters";
  const absl::string_view ConnectionHeaderSanitization = "http1.connection_header_rejected";
  const absl::string_view InvalidUrl = "http1.invalid_url";
  const absl::string_view InvalidTransferEncoding = "http1.invalid_transfer_encoding";
  const absl::string_view BodyDisallowed = "http1.body_disallowed";
  const absl::string_view TransferEncodingNotAllowed = "http1.transfer_encoding_not_allowed";
  const absl::string_view ContentLengthNotAllowed = "http1.content_length_not_allowed";
  const absl::string_view InvalidUnderscore = "http1.unexpected_underscore";
};

struct Http1HeaderTypesValues {
  const absl::string_view Headers = "headers";
  const absl::string_view Trailers = "trailers";
};

using Http1ResponseCodeDetails = ConstSingleton<Http1ResponseCodeDetailValues>;
using Http1HeaderTypes = ConstSingleton<Http1HeaderTypesValues>;
using Http::Http1::CodecStats;
using Http::Http1::HeaderKeyFormatter;
using Http::Http1::HeaderKeyFormatterPtr;
using Http::Http1::ProperCaseHeaderKeyFormatter;

const StringUtil::CaseUnorderedSet& caseUnorderdSetContainingUpgradeAndHttp2Settings() {
  CONSTRUCT_ON_FIRST_USE(StringUtil::CaseUnorderedSet,
                         Http::Headers::get().ConnectionValues.Upgrade,
                         Http::Headers::get().ConnectionValues.Http2Settings);
}

HeaderKeyFormatterPtr formatter(const Http::Http1Settings& settings) {
  if (settings.header_key_format_ == Http1Settings::HeaderKeyFormat::ProperCase) {
    return std::make_unique<ProperCaseHeaderKeyFormatter>();
  }

  return nullptr;
}

} // namespace

const std::string StreamEncoderImpl::CRLF = "\r\n";
// Last chunk as defined here https://tools.ietf.org/html/rfc7230#section-4.1
const std::string StreamEncoderImpl::LAST_CHUNK = "0\r\n";

StreamEncoderImpl::StreamEncoderImpl(ConnectionImpl& connection,
                                     HeaderKeyFormatter* header_key_formatter)
    : connection_(connection), disable_chunk_encoding_(false), chunk_encoding_(true),
      is_response_to_head_request_(false), is_response_to_connect_request_(false),
      header_key_formatter_(header_key_formatter) {
  if (connection_.connection().aboveHighWatermark()) {
    runHighWatermarkCallbacks();
  }
}

void StreamEncoderImpl::encodeHeader(const char* key, uint32_t key_size, const char* value,
                                     uint32_t value_size) {

  ASSERT(key_size > 0);

  connection_.copyToBuffer(key, key_size);
  connection_.addCharToBuffer(':');
  connection_.addCharToBuffer(' ');
  connection_.copyToBuffer(value, value_size);
  connection_.addToBuffer(CRLF);
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

void ResponseEncoderImpl::encode100ContinueHeaders(const ResponseHeaderMap& headers) {
  ASSERT(headers.Status()->value() == "100");
  encodeHeaders(headers, false);
}

void StreamEncoderImpl::encodeHeadersBase(const RequestOrResponseHeaderMap& headers,
                                          absl::optional<uint64_t> status, bool end_stream) {
  bool saw_content_length = false;
  headers.iterate([this](const HeaderEntry& header) -> HeaderMap::Iterate {
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

    encodeFormattedHeader(key_to_use, header.value().getStringView());

    return HeaderMap::Iterate::Continue;
  });

  if (headers.ContentLength()) {
    saw_content_length = true;
  }

  ASSERT(!headers.TransferEncoding());

  // Assume we are chunk encoding unless we are passed a content length or this is a header only
  // response. Upper layers generally should strip transfer-encoding since it only applies to
  // HTTP/1.1. The codec will infer it based on the type of response.
  // for streaming (e.g. SSE stream sent to hystrix dashboard), we do not want
  // chunk transfer encoding but we don't have a content-length so disable_chunk_encoding_ is
  // consulted before enabling chunk encoding.
  //
  // Note that for HEAD requests Envoy does best-effort guessing when there is no
  // content-length. If a client makes a HEAD request for an upstream resource
  // with no bytes but the upstream response doesn't include "Content-length: 0",
  // Envoy will incorrectly assume a subsequent response to GET will be chunk encoded.
  if (saw_content_length || disable_chunk_encoding_) {
    chunk_encoding_ = false;
  } else {
    if (status && *status == 100) {
      // Make sure we don't serialize chunk information with 100-Continue headers.
      chunk_encoding_ = false;
    } else if (end_stream && !is_response_to_head_request_) {
      // If this is a headers-only stream, append an explicit "Content-Length: 0" unless it's a
      // response to a HEAD request.
      // For 204s and 1xx where content length is disallowed, don't append the content length but
      // also don't chunk encode.
      if (!status || (*status >= 200 && *status != 204)) {
        encodeFormattedHeader(Headers::get().ContentLength.get(), "0");
      }
      chunk_encoding_ = false;
    } else if (connection_.protocol() == Protocol::Http10) {
      chunk_encoding_ = false;
    } else if (status && (*status < 200 || *status == 204) &&
               connection_.strict1xxAnd204Headers()) {
      // TODO(zuercher): when the "envoy.reloadable_features.strict_1xx_and_204_response_headers"
      // feature flag is removed, this block can be coalesced with the 100 Continue logic above.

      // For 1xx and 204 responses, do not send the chunked encoding header or enable chunked
      // encoding: https://tools.ietf.org/html/rfc7230#section-3.3.1
      chunk_encoding_ = false;
    } else {
      // For responses to connect requests, do not send the chunked encoding header:
      // https://tools.ietf.org/html/rfc7231#section-4.3.6.
      if (!is_response_to_connect_request_) {
        encodeFormattedHeader(Headers::get().TransferEncoding.get(),
                              Headers::get().TransferEncodingValues.Chunked);
      }
      // We do not apply chunk encoding for HTTP upgrades, including CONNECT style upgrades.
      // If there is a body in a response on the upgrade path, the chunks will be
      // passed through via maybeDirectDispatch so we need to avoid appending
      // extra chunk boundaries.
      //
      // When sending a response to a HEAD request Envoy may send an informational
      // "Transfer-Encoding: chunked" header, but should not send a chunk encoded body.
      chunk_encoding_ = !Utility::isUpgrade(headers) && !is_response_to_head_request_ &&
                        !is_response_to_connect_request_;
    }
  }

  connection_.addToBuffer(CRLF);

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
      connection_.buffer().add(absl::StrCat(absl::Hex(data.length()), CRLF));
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

void StreamEncoderImpl::encodeTrailersBase(const HeaderMap& trailers) {
  if (!connection_.enableTrailers()) {
    return endEncode();
  }
  // Trailers only matter if it is a chunk transfer encoding
  // https://tools.ietf.org/html/rfc7230#section-4.4
  if (chunk_encoding_) {
    // Finalize the body
    connection_.buffer().add(LAST_CHUNK);

    trailers.iterate([this](const HeaderEntry& header) -> HeaderMap::Iterate {
      encodeFormattedHeader(header.key().getStringView(), header.value().getStringView());
      return HeaderMap::Iterate::Continue;
    });

    connection_.flushOutput();
    connection_.buffer().add(CRLF);
  }

  connection_.flushOutput();
  connection_.onEncodeComplete();
}

void StreamEncoderImpl::encodeMetadata(const MetadataMapVector&) {
  connection_.stats().metadata_not_supported_error_.inc();
}

void StreamEncoderImpl::endEncode() {
  if (chunk_encoding_) {
    connection_.buffer().add(LAST_CHUNK);
    connection_.buffer().add(CRLF);
  }

  connection_.flushOutput(true);
  connection_.onEncodeComplete();
}

void ServerConnectionImpl::maybeAddSentinelBufferFragment(Buffer::WatermarkBuffer& output_buffer) {
  if (!flood_protection_) {
    return;
  }
  // It's messy and complicated to try to tag the final write of an HTTP response for response
  // tracking for flood protection. Instead, write an empty buffer fragment after the response,
  // to allow for tracking.
  // When the response is written out, the fragment will be deleted and the counter will be updated
  // by ServerConnectionImpl::releaseOutboundResponse()
  auto fragment =
      Buffer::OwnedBufferFragmentImpl::create(absl::string_view("", 0), response_buffer_releasor_);
  output_buffer.addBufferFragment(*fragment.release());
  ASSERT(outbound_responses_ < max_outbound_responses_);
  outbound_responses_++;
}

void ServerConnectionImpl::doFloodProtectionChecks() const {
  if (!flood_protection_) {
    return;
  }
  // Before processing another request, make sure that we are below the response flood protection
  // threshold.
  if (outbound_responses_ >= max_outbound_responses_) {
    ENVOY_CONN_LOG(trace, "error accepting request: too many pending responses queued",
                   connection_);
    stats_.response_flood_.inc();
    throw FrameFloodException("Too many responses queued.");
  }
}

void ConnectionImpl::flushOutput(bool end_encode) {
  if (end_encode) {
    // If this is an HTTP response in ServerConnectionImpl, track outbound responses for flood
    // protection
    maybeAddSentinelBufferFragment(output_buffer_);
  }
  connection().write(output_buffer_, false);
  ASSERT(0UL == output_buffer_.length());
}

void ConnectionImpl::addToBuffer(absl::string_view data) { output_buffer_.add(data); }

void ConnectionImpl::addCharToBuffer(char c) { output_buffer_.add(&c, 1); }

void ConnectionImpl::addIntToBuffer(uint64_t i) { output_buffer_.add(absl::StrCat(i)); }

void ConnectionImpl::copyToBuffer(const char* data, uint64_t length) {
  output_buffer_.add(data, length);
}

void StreamEncoderImpl::resetStream(StreamResetReason reason) {
  connection_.onResetStreamBase(reason);
}

void StreamEncoderImpl::readDisable(bool disable) {
  if (disable) {
    ++read_disable_calls_;
  } else {
    ASSERT(read_disable_calls_ != 0);
    if (read_disable_calls_ != 0) {
      --read_disable_calls_;
    }
  }
  connection_.readDisable(disable);
}

uint32_t StreamEncoderImpl::bufferLimit() { return connection_.bufferLimit(); }

const Network::Address::InstanceConstSharedPtr& StreamEncoderImpl::connectionLocalAddress() {
  return connection_.connection().localAddress();
}

static const char RESPONSE_PREFIX[] = "HTTP/1.1 ";
static const char HTTP_10_RESPONSE_PREFIX[] = "HTTP/1.0 ";

void ResponseEncoderImpl::encodeHeaders(const ResponseHeaderMap& headers, bool end_stream) {
  started_response_ = true;

  // The contract is that client codecs must ensure that :status is present.
  ASSERT(headers.Status() != nullptr);
  uint64_t numeric_status = Utility::getResponseStatus(headers);

  if (connection_.protocol() == Protocol::Http10 && connection_.supportsHttp10()) {
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

  if (numeric_status >= 300) {
    // Don't do special CONNECT logic if the CONNECT was rejected.
    is_response_to_connect_request_ = false;
  }

  encodeHeadersBase(headers, absl::make_optional<uint64_t>(numeric_status), end_stream);
}

static const char REQUEST_POSTFIX[] = " HTTP/1.1\r\n";

void RequestEncoderImpl::encodeHeaders(const RequestHeaderMap& headers, bool end_stream) {
  const HeaderEntry* method = headers.Method();
  const HeaderEntry* path = headers.Path();
  const HeaderEntry* host = headers.Host();
  bool is_connect = HeaderUtility::isConnect(headers);

  if (!method || (!path && !is_connect)) {
    // TODO(#10878): This exception does not occur during dispatch and would not be triggered under
    // normal circumstances since inputs would fail parsing at ingress. Replace with proper error
    // handling when exceptions are removed. Include missing host header for CONNECT.
    throw CodecClientException(":method and :path must be specified");
  }
  if (method->value() == Headers::get().MethodValues.Head) {
    head_request_ = true;
  } else if (method->value() == Headers::get().MethodValues.Connect) {
    disableChunkEncoding();
    connect_request_ = true;
  }
  if (Utility::isUpgrade(headers)) {
    upgrade_request_ = true;
  }

  connection_.copyToBuffer(method->value().getStringView().data(), method->value().size());
  connection_.addCharToBuffer(' ');
  if (is_connect) {
    connection_.copyToBuffer(host->value().getStringView().data(), host->value().size());
  } else {
    connection_.copyToBuffer(path->value().getStringView().data(), path->value().size());
  }
  connection_.copyToBuffer(REQUEST_POSTFIX, sizeof(REQUEST_POSTFIX) - 1);

  encodeHeadersBase(headers, absl::nullopt, end_stream);
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
      static_cast<ConnectionImpl*>(parser->data)->bufferBody(at, length);
      return 0;
    },
    [](http_parser* parser) -> int {
      static_cast<ConnectionImpl*>(parser->data)->onMessageCompleteBase();
      return 0;
    },
    [](http_parser* parser) -> int {
      // A 0-byte chunk header is used to signal the end of the chunked body.
      // When this function is called, http-parser holds the size of the chunk in
      // parser->content_length. See
      // https://github.com/nodejs/http-parser/blob/v2.9.3/http_parser.h#L336
      const bool is_final_chunk = (parser->content_length == 0);
      static_cast<ConnectionImpl*>(parser->data)->onChunkHeader(is_final_chunk);
      return 0;
    },
    nullptr // on_chunk_complete
};

ConnectionImpl::ConnectionImpl(Network::Connection& connection, CodecStats& stats,
                               http_parser_type type, uint32_t max_headers_kb,
                               const uint32_t max_headers_count,
                               HeaderKeyFormatterPtr&& header_key_formatter, bool enable_trailers)
    : connection_(connection), stats_(stats),
      header_key_formatter_(std::move(header_key_formatter)), processing_trailers_(false),
      handling_upgrade_(false), reset_stream_called_(false), deferred_end_stream_headers_(false),
      connection_header_sanitization_(Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.connection_header_sanitization")),
      enable_trailers_(enable_trailers),
      strict_1xx_and_204_headers_(Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.strict_1xx_and_204_response_headers")),
      output_buffer_([&]() -> void { this->onBelowLowWatermark(); },
                     [&]() -> void { this->onAboveHighWatermark(); },
                     []() -> void { /* TODO(adisuissa): Handle overflow watermark */ }),
      max_headers_kb_(max_headers_kb), max_headers_count_(max_headers_count) {
  output_buffer_.setWatermarks(connection.bufferLimit());
  http_parser_init(&parser_, type);
  parser_.data = this;
}

void ConnectionImpl::completeLastHeader() {
  ENVOY_CONN_LOG(trace, "completed header: key={} value={}", connection_,
                 current_header_field_.getStringView(), current_header_value_.getStringView());

  checkHeaderNameForUnderscores();
  auto& headers_or_trailers = headersOrTrailers();
  if (!current_header_field_.empty()) {
    current_header_field_.inlineTransform([](char c) { return absl::ascii_tolower(c); });
    // Strip trailing whitespace of the current header value if any. Leading whitespace was trimmed
    // in ConnectionImpl::onHeaderValue. http_parser does not strip leading or trailing whitespace
    // as the spec requires: https://tools.ietf.org/html/rfc7230#section-3.2.4
    current_header_value_.rtrim();
    headers_or_trailers.addViaMove(std::move(current_header_field_),
                                   std::move(current_header_value_));
  }

  // Check if the number of headers exceeds the limit.
  if (headers_or_trailers.size() > max_headers_count_) {
    error_code_ = Http::Code::RequestHeaderFieldsTooLarge;
    sendProtocolError(Http1ResponseCodeDetails::get().TooManyHeaders);
    const absl::string_view header_type =
        processing_trailers_ ? Http1HeaderTypes::get().Trailers : Http1HeaderTypes::get().Headers;
    throw CodecProtocolException(absl::StrCat(header_type, " size exceeds limit"));
  }

  header_parsing_state_ = HeaderParsingState::Field;
  ASSERT(current_header_field_.empty());
  ASSERT(current_header_value_.empty());
}

uint32_t ConnectionImpl::getHeadersSize() {
  return current_header_field_.size() + current_header_value_.size() +
         headersOrTrailers().byteSize();
}

void ConnectionImpl::checkMaxHeadersSize() {
  const uint32_t total = getHeadersSize();
  if (total > (max_headers_kb_ * 1024)) {
    const absl::string_view header_type =
        processing_trailers_ ? Http1HeaderTypes::get().Trailers : Http1HeaderTypes::get().Headers;
    error_code_ = Http::Code::RequestHeaderFieldsTooLarge;
    sendProtocolError(Http1ResponseCodeDetails::get().HeadersTooLarge);
    throw CodecProtocolException(absl::StrCat(header_type, " size exceeds limit"));
  }
}

bool ConnectionImpl::maybeDirectDispatch(Buffer::Instance& data) {
  if (!handling_upgrade_) {
    // Only direct dispatch for Upgrade requests.
    return false;
  }

  ENVOY_CONN_LOG(trace, "direct-dispatched {} bytes", connection_, data.length());
  onBody(data);
  data.drain(data.length());
  return true;
}

Http::Status ConnectionImpl::dispatch(Buffer::Instance& data) {
  // TODO(#10878): Remove this wrapper when exception removal is complete. innerDispatch may either
  // throw an exception or return an error status. The utility wrapper catches exceptions and
  // converts them to error statuses.
  return Utility::exceptionToStatus(
      [&](Buffer::Instance& data) -> Http::Status { return innerDispatch(data); }, data);
}

Http::Status ConnectionImpl::innerDispatch(Buffer::Instance& data) {
  ENVOY_CONN_LOG(trace, "parsing {} bytes", connection_, data.length());
  ASSERT(buffered_body_.length() == 0);

  if (maybeDirectDispatch(data)) {
    return Http::okStatus();
  }

  // Always unpause before dispatch.
  http_parser_pause(&parser_, 0);

  ssize_t total_parsed = 0;
  if (data.length() > 0) {
    for (const Buffer::RawSlice& slice : data.getRawSlices()) {
      total_parsed += dispatchSlice(static_cast<const char*>(slice.mem_), slice.len_);
      if (HTTP_PARSER_ERRNO(&parser_) != HPE_OK) {
        // Parse errors trigger an exception in dispatchSlice so we are guaranteed to be paused at
        // this point.
        ASSERT(HTTP_PARSER_ERRNO(&parser_) == HPE_PAUSED);
        break;
      }
    }
    dispatchBufferedBody();
  } else {
    dispatchSlice(nullptr, 0);
  }
  ASSERT(buffered_body_.length() == 0);

  ENVOY_CONN_LOG(trace, "parsed {} bytes", connection_, total_parsed);
  data.drain(total_parsed);

  // If an upgrade has been handled and there is body data or early upgrade
  // payload to send on, send it on.
  maybeDirectDispatch(data);
  return Http::okStatus();
}

size_t ConnectionImpl::dispatchSlice(const char* slice, size_t len) {
  ssize_t rc = http_parser_execute(&parser_, &settings_, slice, len);
  if (HTTP_PARSER_ERRNO(&parser_) != HPE_OK && HTTP_PARSER_ERRNO(&parser_) != HPE_PAUSED) {
    sendProtocolError(Http1ResponseCodeDetails::get().HttpCodecError);
    throw CodecProtocolException("http/1.1 protocol error: " +
                                 std::string(http_errno_name(HTTP_PARSER_ERRNO(&parser_))));
  }

  return rc;
}

void ConnectionImpl::onHeaderField(const char* data, size_t length) {
  // We previously already finished up the headers, these headers are
  // now trailers.
  if (header_parsing_state_ == HeaderParsingState::Done) {
    if (!enable_trailers_) {
      // Ignore trailers.
      return;
    }
    processing_trailers_ = true;
    header_parsing_state_ = HeaderParsingState::Field;
    allocTrailers();
  }
  if (header_parsing_state_ == HeaderParsingState::Value) {
    completeLastHeader();
  }

  current_header_field_.append(data, length);

  checkMaxHeadersSize();
}

void ConnectionImpl::onHeaderValue(const char* data, size_t length) {
  if (header_parsing_state_ == HeaderParsingState::Done && !enable_trailers_) {
    // Ignore trailers.
    return;
  }

  absl::string_view header_value{data, length};
  if (!Http::HeaderUtility::headerValueIsValid(header_value)) {
    ENVOY_CONN_LOG(debug, "invalid header value: {}", connection_, header_value);
    error_code_ = Http::Code::BadRequest;
    sendProtocolError(Http1ResponseCodeDetails::get().InvalidCharacters);
    throw CodecProtocolException("http/1.1 protocol error: header value contains invalid chars");
  }

  header_parsing_state_ = HeaderParsingState::Value;
  if (current_header_value_.empty()) {
    // Strip leading whitespace if the current header value input contains the first bytes of the
    // encoded header value. Trailing whitespace is stripped once the full header value is known in
    // ConnectionImpl::completeLastHeader. http_parser does not strip leading or trailing whitespace
    // as the spec requires: https://tools.ietf.org/html/rfc7230#section-3.2.4 .
    header_value = StringUtil::ltrim(header_value);
  }
  current_header_value_.append(header_value.data(), header_value.length());

  checkMaxHeadersSize();
}

int ConnectionImpl::onHeadersCompleteBase() {
  ASSERT(!processing_trailers_);
  ENVOY_CONN_LOG(trace, "onHeadersCompleteBase", connection_);
  completeLastHeader();

  if (!(parser_.http_major == 1 && parser_.http_minor == 1)) {
    // This is not necessarily true, but it's good enough since higher layers only care if this is
    // HTTP/1.1 or not.
    protocol_ = Protocol::Http10;
  }
  RequestOrResponseHeaderMap& request_or_response_headers = requestOrResponseHeaders();
  if (Utility::isUpgrade(request_or_response_headers) && upgradeAllowed()) {
    // Ignore h2c upgrade requests until we support them.
    // See https://github.com/envoyproxy/envoy/issues/7161 for details.
    if (absl::EqualsIgnoreCase(request_or_response_headers.getUpgradeValue(),
                               Http::Headers::get().UpgradeValues.H2c)) {
      ENVOY_CONN_LOG(trace, "removing unsupported h2c upgrade headers.", connection_);
      request_or_response_headers.removeUpgrade();
      if (request_or_response_headers.Connection()) {
        const auto& tokens_to_remove = caseUnorderdSetContainingUpgradeAndHttp2Settings();
        std::string new_value = StringUtil::removeTokens(
            request_or_response_headers.getConnectionValue(), ",", tokens_to_remove, ",");
        if (new_value.empty()) {
          request_or_response_headers.removeConnection();
        } else {
          request_or_response_headers.setConnection(new_value);
        }
      }
      request_or_response_headers.remove(Headers::get().Http2Settings);
    } else {
      ENVOY_CONN_LOG(trace, "codec entering upgrade mode.", connection_);
      handling_upgrade_ = true;
    }
  }
  if (parser_.method == HTTP_CONNECT) {
    if (request_or_response_headers.ContentLength()) {
      if (request_or_response_headers.getContentLengthValue() == "0") {
        request_or_response_headers.removeContentLength();
      } else {
        // Per https://tools.ietf.org/html/rfc7231#section-4.3.6 a payload with a
        // CONNECT request has no defined semantics, and may be rejected.
        error_code_ = Http::Code::BadRequest;
        sendProtocolError(Http1ResponseCodeDetails::get().BodyDisallowed);
        throw CodecProtocolException("http/1.1 protocol error: unsupported content length");
      }
    }
    ENVOY_CONN_LOG(trace, "codec entering upgrade mode for CONNECT request.", connection_);
    handling_upgrade_ = true;
  }

  // Per https://tools.ietf.org/html/rfc7230#section-3.3.1 Envoy should reject
  // transfer-codings it does not understand.
  // Per https://tools.ietf.org/html/rfc7231#section-4.3.6 a payload with a
  // CONNECT request has no defined semantics, and may be rejected.
  if (request_or_response_headers.TransferEncoding()) {
    const absl::string_view encoding = request_or_response_headers.getTransferEncodingValue();
    if (!absl::EqualsIgnoreCase(encoding, Headers::get().TransferEncodingValues.Chunked) ||
        parser_.method == HTTP_CONNECT) {
      error_code_ = Http::Code::NotImplemented;
      sendProtocolError(Http1ResponseCodeDetails::get().InvalidTransferEncoding);
      throw CodecProtocolException("http/1.1 protocol error: unsupported transfer encoding");
    }
  }

  int rc = onHeadersComplete();
  header_parsing_state_ = HeaderParsingState::Done;

  // Returning 2 informs http_parser to not expect a body or further data on this connection.
  return handling_upgrade_ ? 2 : rc;
}

void ConnectionImpl::bufferBody(const char* data, size_t length) {
  buffered_body_.add(data, length);
}

void ConnectionImpl::dispatchBufferedBody() {
  ASSERT(HTTP_PARSER_ERRNO(&parser_) == HPE_OK || HTTP_PARSER_ERRNO(&parser_) == HPE_PAUSED);
  if (buffered_body_.length() > 0) {
    onBody(buffered_body_);
    buffered_body_.drain(buffered_body_.length());
  }
}

void ConnectionImpl::onChunkHeader(bool is_final_chunk) {
  if (is_final_chunk) {
    // Dispatch body before parsing trailers, so body ends up dispatched even if an error is found
    // while processing trailers.
    dispatchBufferedBody();
  }
}

void ConnectionImpl::onMessageCompleteBase() {
  ENVOY_CONN_LOG(trace, "message complete", connection_);

  dispatchBufferedBody();

  if (handling_upgrade_) {
    // If this is an upgrade request, swallow the onMessageComplete. The
    // upgrade payload will be treated as stream body.
    ASSERT(!deferred_end_stream_headers_);
    ENVOY_CONN_LOG(trace, "Pausing parser due to upgrade.", connection_);
    http_parser_pause(&parser_, 1);
    return;
  }

  // If true, this indicates we were processing trailers and must
  // move the last header into current_header_map_
  if (header_parsing_state_ == HeaderParsingState::Value) {
    completeLastHeader();
  }

  onMessageComplete();
}

void ConnectionImpl::onMessageBeginBase() {
  ENVOY_CONN_LOG(trace, "message begin", connection_);
  // Make sure that if HTTP/1.0 and HTTP/1.1 requests share a connection Envoy correctly sets
  // protocol for each request. Envoy defaults to 1.1 but sets the protocol to 1.0 where applicable
  // in onHeadersCompleteBase
  protocol_ = Protocol::Http11;
  processing_trailers_ = false;
  header_parsing_state_ = HeaderParsingState::Field;
  allocHeaders();
  onMessageBegin();
}

void ConnectionImpl::onResetStreamBase(StreamResetReason reason) {
  ASSERT(!reset_stream_called_);
  reset_stream_called_ = true;
  onResetStream(reason);
}

ServerConnectionImpl::ServerConnectionImpl(
    Network::Connection& connection, CodecStats& stats, ServerConnectionCallbacks& callbacks,
    const Http1Settings& settings, uint32_t max_request_headers_kb,
    const uint32_t max_request_headers_count,
    envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
        headers_with_underscores_action)
    : ConnectionImpl(connection, stats, HTTP_REQUEST, max_request_headers_kb,
                     max_request_headers_count, formatter(settings), settings.enable_trailers_),
      callbacks_(callbacks), codec_settings_(settings),
      response_buffer_releasor_([this](const Buffer::OwnedBufferFragmentImpl* fragment) {
        releaseOutboundResponse(fragment);
      }),
      // Pipelining is generally not well supported on the internet and has a series of dangerous
      // overflow bugs. As such we are disabling it for now, and removing this temporary override if
      // no one objects. If you use this integer to restore prior behavior, contact the
      // maintainer team as it will otherwise be removed entirely soon.
      max_outbound_responses_(
          Runtime::getInteger("envoy.do_not_use_going_away_max_http2_outbound_responses", 2)),
      flood_protection_(
          Runtime::runtimeFeatureEnabled("envoy.reloadable_features.http1_flood_protection")),
      headers_with_underscores_action_(headers_with_underscores_action) {}

uint32_t ServerConnectionImpl::getHeadersSize() {
  // Add in the the size of the request URL if processing request headers.
  const uint32_t url_size = (!processing_trailers_ && active_request_.has_value())
                                ? active_request_.value().request_url_.size()
                                : 0;
  return url_size + ConnectionImpl::getHeadersSize();
}

void ServerConnectionImpl::onEncodeComplete() {
  if (active_request_.value().remote_complete_) {
    // Only do this if remote is complete. If we are replying before the request is complete the
    // only logical thing to do is for higher level code to reset() / close the connection so we
    // leave the request around so that it can fire reset callbacks.
    active_request_.reset();
  }
}

void ServerConnectionImpl::handlePath(RequestHeaderMap& headers, unsigned int method) {
  HeaderString path(Headers::get().Path);

  bool is_connect = (method == HTTP_CONNECT);

  // The url is relative or a wildcard when the method is OPTIONS. Nothing to do here.
  auto& active_request = active_request_.value();
  if (!is_connect && !active_request.request_url_.getStringView().empty() &&
      (active_request.request_url_.getStringView()[0] == '/' ||
       ((method == HTTP_OPTIONS) && active_request.request_url_.getStringView()[0] == '*'))) {
    headers.addViaMove(std::move(path), std::move(active_request.request_url_));
    return;
  }

  // If absolute_urls and/or connect are not going be handled, copy the url and return.
  // This forces the behavior to be backwards compatible with the old codec behavior.
  // CONNECT "urls" are actually host:port so look like absolute URLs to the above checks.
  // Absolute URLS in CONNECT requests will be rejected below by the URL class validation.
  if (!codec_settings_.allow_absolute_url_ && !is_connect) {
    headers.addViaMove(std::move(path), std::move(active_request.request_url_));
    return;
  }

  Utility::Url absolute_url;
  if (!absolute_url.initialize(active_request.request_url_.getStringView(), is_connect)) {
    sendProtocolError(Http1ResponseCodeDetails::get().InvalidUrl);
    throw CodecProtocolException("http/1.1 protocol error: invalid url in request line");
  }
  // RFC7230#5.7
  // When a proxy receives a request with an absolute-form of
  // request-target, the proxy MUST ignore the received Host header field
  // (if any) and instead replace it with the host information of the
  // request-target. A proxy that forwards such a request MUST generate a
  // new Host field-value based on the received request-target rather than
  // forward the received Host field-value.
  headers.setHost(absolute_url.hostAndPort());

  if (!absolute_url.pathAndQueryParams().empty()) {
    headers.setPath(absolute_url.pathAndQueryParams());
  }
  active_request.request_url_.clear();
}

int ServerConnectionImpl::onHeadersComplete() {
  // Handle the case where response happens prior to request complete. It's up to upper layer code
  // to disconnect the connection but we shouldn't fire any more events since it doesn't make
  // sense.
  if (active_request_.has_value()) {
    auto& active_request = active_request_.value();
    auto& headers = absl::get<RequestHeaderMapPtr>(headers_or_trailers_);
    ENVOY_CONN_LOG(trace, "Server: onHeadersComplete size={}", connection_, headers->size());
    const char* method_string = http_method_str(static_cast<http_method>(parser_.method));

    if (!handling_upgrade_ && connection_header_sanitization_ && headers->Connection()) {
      // If we fail to sanitize the request, return a 400 to the client
      if (!Utility::sanitizeConnectionHeader(*headers)) {
        absl::string_view header_value = headers->getConnectionValue();
        ENVOY_CONN_LOG(debug, "Invalid nominated headers in Connection: {}", connection_,
                       header_value);
        error_code_ = Http::Code::BadRequest;
        sendProtocolError(Http1ResponseCodeDetails::get().ConnectionHeaderSanitization);
        throw CodecProtocolException("Invalid nominated headers in Connection.");
      }
    }

    // Inform the response encoder about any HEAD method, so it can set content
    // length and transfer encoding headers correctly.
    active_request.response_encoder_.setIsResponseToHeadRequest(parser_.method == HTTP_HEAD);
    active_request.response_encoder_.setIsResponseToConnectRequest(parser_.method == HTTP_CONNECT);

    handlePath(*headers, parser_.method);
    ASSERT(active_request.request_url_.empty());

    headers->setMethod(method_string);

    // Make sure the host is valid.
    auto details = HeaderUtility::requestHeadersValid(*headers);
    if (details.has_value()) {
      sendProtocolError(details.value().get());
      throw CodecProtocolException(
          "http/1.1 protocol error: request headers failed spec compliance checks");
    }

    // Determine here whether we have a body or not. This uses the new RFC semantics where the
    // presence of content-length or chunked transfer-encoding indicates a body vs. a particular
    // method. If there is no body, we defer raising decodeHeaders() until the parser is flushed
    // with message complete. This allows upper layers to behave like HTTP/2 and prevents a proxy
    // scenario where the higher layers stream through and implicitly switch to chunked transfer
    // encoding because end stream with zero body length has not yet been indicated.
    if (parser_.flags & F_CHUNKED ||
        (parser_.content_length > 0 && parser_.content_length != ULLONG_MAX) || handling_upgrade_) {
      active_request.request_decoder_->decodeHeaders(std::move(headers), false);

      // If the connection has been closed (or is closing) after decoding headers, pause the parser
      // so we return control to the caller.
      if (connection_.state() != Network::Connection::State::Open) {
        http_parser_pause(&parser_, 1);
      }
    } else {
      deferred_end_stream_headers_ = true;
    }
  }

  return 0;
}

void ServerConnectionImpl::onMessageBegin() {
  if (!resetStreamCalled()) {
    ASSERT(!active_request_.has_value());
    active_request_.emplace(*this, header_key_formatter_.get());
    auto& active_request = active_request_.value();
    active_request.request_decoder_ = &callbacks_.newStream(active_request.response_encoder_);

    // Check for pipelined request flood as we prepare to accept a new request.
    // Parse errors that happen prior to onMessageBegin result in stream termination, it is not
    // possible to overflow output buffers with early parse errors.
    doFloodProtectionChecks();
  }
}

void ServerConnectionImpl::onUrl(const char* data, size_t length) {
  if (active_request_.has_value()) {
    active_request_.value().request_url_.append(data, length);

    checkMaxHeadersSize();
  }
}

void ServerConnectionImpl::onBody(Buffer::Instance& data) {
  ASSERT(!deferred_end_stream_headers_);
  if (active_request_.has_value()) {
    ENVOY_CONN_LOG(trace, "body size={}", connection_, data.length());
    active_request_.value().request_decoder_->decodeData(data, false);
  }
}

void ServerConnectionImpl::onMessageComplete() {
  ASSERT(!handling_upgrade_);
  if (active_request_.has_value()) {
    auto& active_request = active_request_.value();

    if (active_request.request_decoder_) {
      active_request.response_encoder_.readDisable(true);
    }
    active_request.remote_complete_ = true;
    if (deferred_end_stream_headers_) {
      active_request.request_decoder_->decodeHeaders(
          std::move(absl::get<RequestHeaderMapPtr>(headers_or_trailers_)), true);
      deferred_end_stream_headers_ = false;
    } else if (processing_trailers_) {
      active_request.request_decoder_->decodeTrailers(
          std::move(absl::get<RequestTrailerMapPtr>(headers_or_trailers_)));
    } else {
      Buffer::OwnedImpl buffer;
      active_request.request_decoder_->decodeData(buffer, true);
    }

    // Reset to ensure no information from one requests persists to the next.
    headers_or_trailers_.emplace<RequestHeaderMapPtr>(nullptr);
  }

  // Always pause the parser so that the calling code can process 1 request at a time and apply
  // back pressure. However this means that the calling code needs to detect if there is more data
  // in the buffer and dispatch it again.
  http_parser_pause(&parser_, 1);
}

void ServerConnectionImpl::onResetStream(StreamResetReason reason) {
  active_request_.value().response_encoder_.runResetCallbacks(reason);
  active_request_.reset();
}

void ServerConnectionImpl::sendProtocolErrorOld(absl::string_view details) {
  if (active_request_.has_value()) {
    active_request_.value().response_encoder_.setDetails(details);
  }
  // We do this here because we may get a protocol error before we have a logical stream. Higher
  // layers can only operate on streams, so there is no coherent way to allow them to send an error
  // "out of band." On one hand this is kind of a hack but on the other hand it normalizes HTTP/1.1
  // to look more like HTTP/2 to higher layers.
  if (!active_request_.has_value() ||
      !active_request_.value().response_encoder_.startedResponse()) {
    Buffer::OwnedImpl bad_request_response(
        absl::StrCat("HTTP/1.1 ", error_code_, " ", CodeUtility::toString(error_code_),
                     "\r\ncontent-length: 0\r\nconnection: close\r\n\r\n"));

    connection_.write(bad_request_response, false);
  }
}

void ServerConnectionImpl::sendProtocolError(absl::string_view details) {
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.early_errors_via_hcm")) {
    sendProtocolErrorOld(details);
    return;
  }
  // We do this here because we may get a protocol error before we have a logical stream.
  if (!active_request_.has_value()) {
    onMessageBeginBase();
  }
  ASSERT(active_request_.has_value());

  active_request_.value().response_encoder_.setDetails(details);
  if (!active_request_.value().response_encoder_.startedResponse()) {
    // Note that the correctness of is_grpc_request and is_head_request is best-effort.
    // If headers have not been fully parsed they may not be inferred correctly.
    bool is_grpc_request = false;
    if (absl::holds_alternative<RequestHeaderMapPtr>(headers_or_trailers_) &&
        absl::get<RequestHeaderMapPtr>(headers_or_trailers_) != nullptr) {
      is_grpc_request =
          Grpc::Common::isGrpcRequestHeaders(*absl::get<RequestHeaderMapPtr>(headers_or_trailers_));
    }
    const bool is_head_request = parser_.method == HTTP_HEAD;
    active_request_->request_decoder_->sendLocalReply(is_grpc_request, error_code_,
                                                      CodeUtility::toString(error_code_), nullptr,
                                                      is_head_request, absl::nullopt, details);
    return;
  }
}

void ServerConnectionImpl::onAboveHighWatermark() {
  if (active_request_.has_value()) {
    active_request_.value().response_encoder_.runHighWatermarkCallbacks();
  }
}
void ServerConnectionImpl::onBelowLowWatermark() {
  if (active_request_.has_value()) {
    active_request_.value().response_encoder_.runLowWatermarkCallbacks();
  }
}

void ServerConnectionImpl::releaseOutboundResponse(
    const Buffer::OwnedBufferFragmentImpl* fragment) {
  ASSERT(outbound_responses_ >= 1);
  --outbound_responses_;
  delete fragment;
}

void ServerConnectionImpl::checkHeaderNameForUnderscores() {
  if (headers_with_underscores_action_ != envoy::config::core::v3::HttpProtocolOptions::ALLOW &&
      Http::HeaderUtility::headerNameContainsUnderscore(current_header_field_.getStringView())) {
    if (headers_with_underscores_action_ ==
        envoy::config::core::v3::HttpProtocolOptions::DROP_HEADER) {
      ENVOY_CONN_LOG(debug, "Dropping header with invalid characters in its name: {}", connection_,
                     current_header_field_.getStringView());
      stats_.dropped_headers_with_underscores_.inc();
      current_header_field_.clear();
      current_header_value_.clear();
    } else {
      ENVOY_CONN_LOG(debug, "Rejecting request due to header name with underscores: {}",
                     connection_, current_header_field_.getStringView());
      error_code_ = Http::Code::BadRequest;
      sendProtocolError(Http1ResponseCodeDetails::get().InvalidUnderscore);
      stats_.requests_rejected_with_underscores_in_headers_.inc();
      throw CodecProtocolException("http/1.1 protocol error: header name contains underscores");
    }
  }
}

ClientConnectionImpl::ClientConnectionImpl(Network::Connection& connection, CodecStats& stats,
                                           ConnectionCallbacks&, const Http1Settings& settings,
                                           const uint32_t max_response_headers_count)
    : ConnectionImpl(connection, stats, HTTP_RESPONSE, MAX_RESPONSE_HEADERS_KB,
                     max_response_headers_count, formatter(settings), settings.enable_trailers_) {}

bool ClientConnectionImpl::cannotHaveBody() {
  if (pending_response_.has_value() && pending_response_.value().encoder_.headRequest()) {
    ASSERT(!pending_response_done_);
    return true;
  } else if (parser_.status_code == 204 || parser_.status_code == 304 ||
             (parser_.status_code >= 200 && parser_.content_length == 0)) {
    return true;
  } else {
    return false;
  }
}

RequestEncoder& ClientConnectionImpl::newStream(ResponseDecoder& response_decoder) {
  if (resetStreamCalled()) {
    throw CodecClientException("cannot create new streams after calling reset");
  }

  // If reads were disabled due to flow control, we expect reads to always be enabled again before
  // reusing this connection. This is done when the response is received.
  ASSERT(connection_.readEnabled());

  ASSERT(!pending_response_.has_value());
  ASSERT(pending_response_done_);
  pending_response_.emplace(*this, header_key_formatter_.get(), &response_decoder);
  pending_response_done_ = false;
  return pending_response_.value().encoder_;
}

int ClientConnectionImpl::onHeadersComplete() {
  // Handle the case where the client is closing a kept alive connection (by sending a 408
  // with a 'Connection: close' header). In this case we just let response flush out followed
  // by the remote close.
  if (!pending_response_.has_value() && !resetStreamCalled()) {
    throw PrematureResponseException(static_cast<Http::Code>(parser_.status_code));
  } else if (pending_response_.has_value()) {
    ASSERT(!pending_response_done_);
    auto& headers = absl::get<ResponseHeaderMapPtr>(headers_or_trailers_);
    ENVOY_CONN_LOG(trace, "Client: onHeadersComplete size={}", connection_, headers->size());
    headers->setStatus(parser_.status_code);

    if (parser_.status_code >= 200 && parser_.status_code < 300 &&
        pending_response_.value().encoder_.connectRequest()) {
      ENVOY_CONN_LOG(trace, "codec entering upgrade mode for CONNECT response.", connection_);
      handling_upgrade_ = true;

      // For responses to connect requests, do not accept the chunked
      // encoding header: https://tools.ietf.org/html/rfc7231#section-4.3.6
      if (headers->TransferEncoding() &&
          absl::EqualsIgnoreCase(headers->TransferEncoding()->value().getStringView(),
                                 Headers::get().TransferEncodingValues.Chunked)) {
        sendProtocolError(Http1ResponseCodeDetails::get().InvalidTransferEncoding);
        throw CodecProtocolException("http/1.1 protocol error: unsupported transfer encoding");
      }
    }

    if (strict_1xx_and_204_headers_ && (parser_.status_code < 200 || parser_.status_code == 204)) {
      if (headers->TransferEncoding()) {
        sendProtocolError(Http1ResponseCodeDetails::get().TransferEncodingNotAllowed);
        throw CodecProtocolException(
            "http/1.1 protocol error: transfer encoding not allowed in 1xx or 204");
      }

      if (headers->ContentLength()) {
        // Report a protocol error for non-zero Content-Length, but paper over zero Content-Length.
        if (headers->ContentLength()->value().getStringView() != "0") {
          sendProtocolError(Http1ResponseCodeDetails::get().ContentLengthNotAllowed);
          throw CodecProtocolException(
              "http/1.1 protocol error: content length not allowed in 1xx or 204");
        }

        headers->removeContentLength();
      }
    }

    if (parser_.status_code == enumToInt(Http::Code::Continue)) {
      pending_response_.value().decoder_->decode100ContinueHeaders(std::move(headers));
    } else if (cannotHaveBody() && !handling_upgrade_) {
      deferred_end_stream_headers_ = true;
    } else {
      pending_response_.value().decoder_->decodeHeaders(std::move(headers), false);
    }

    // http-parser treats 1xx headers as their own complete response. Swallow the spurious
    // onMessageComplete and continue processing for purely informational headers.
    // 101-SwitchingProtocols is exempt as all data after the header is proxied through after
    // upgrading.
    if (CodeUtility::is1xx(parser_.status_code) &&
        parser_.status_code != enumToInt(Http::Code::SwitchingProtocols)) {
      ignore_message_complete_for_1xx_ = true;
      // Reset to ensure no information from the 1xx headers is used for the response headers.
      headers_or_trailers_.emplace<ResponseHeaderMapPtr>(nullptr);
    }
  }

  // Here we deal with cases where the response cannot have a body, but http_parser does not deal
  // with it for us.
  return cannotHaveBody() ? 1 : 0;
}

bool ClientConnectionImpl::upgradeAllowed() const {
  if (pending_response_.has_value()) {
    return pending_response_->encoder_.upgradeRequest();
  }
  return false;
}

void ClientConnectionImpl::onBody(Buffer::Instance& data) {
  ASSERT(!deferred_end_stream_headers_);
  if (pending_response_.has_value()) {
    ASSERT(!pending_response_done_);
    pending_response_.value().decoder_->decodeData(data, false);
  }
}

void ClientConnectionImpl::onMessageComplete() {
  ENVOY_CONN_LOG(trace, "message complete", connection_);
  if (ignore_message_complete_for_1xx_) {
    ignore_message_complete_for_1xx_ = false;
    return;
  }
  if (pending_response_.has_value()) {
    ASSERT(!pending_response_done_);
    // After calling decodeData() with end stream set to true, we should no longer be able to reset.
    PendingResponse& response = pending_response_.value();
    // Encoder is used as part of decode* calls later in this function so pending_response_ can not
    // be reset just yet. Preserve the state in pending_response_done_ instead.
    pending_response_done_ = true;

    if (deferred_end_stream_headers_) {
      response.decoder_->decodeHeaders(
          std::move(absl::get<ResponseHeaderMapPtr>(headers_or_trailers_)), true);
      deferred_end_stream_headers_ = false;
    } else if (processing_trailers_) {
      response.decoder_->decodeTrailers(
          std::move(absl::get<ResponseTrailerMapPtr>(headers_or_trailers_)));
    } else {
      Buffer::OwnedImpl buffer;
      response.decoder_->decodeData(buffer, true);
    }

    // Reset to ensure no information from one requests persists to the next.
    pending_response_.reset();
    headers_or_trailers_.emplace<ResponseHeaderMapPtr>(nullptr);
  }
}

void ClientConnectionImpl::onResetStream(StreamResetReason reason) {
  // Only raise reset if we did not already dispatch a complete response.
  if (pending_response_.has_value() && !pending_response_done_) {
    pending_response_.value().encoder_.runResetCallbacks(reason);
    pending_response_done_ = true;
    pending_response_.reset();
  }
}

void ClientConnectionImpl::sendProtocolError(absl::string_view details) {
  if (pending_response_.has_value()) {
    ASSERT(!pending_response_done_);
    pending_response_.value().encoder_.setDetails(details);
  }
}

void ClientConnectionImpl::onAboveHighWatermark() {
  // This should never happen without an active stream/request.
  pending_response_.value().encoder_.runHighWatermarkCallbacks();
}

void ClientConnectionImpl::onBelowLowWatermark() {
  // This can get called without an active stream/request when the response completion causes us to
  // close the connection, but in doing so go below low watermark.
  if (pending_response_.has_value() && !pending_response_done_) {
    pending_response_.value().encoder_.runLowWatermarkCallbacks();
  }
}

} // namespace Http1
} // namespace Legacy
} // namespace Http
} // namespace Envoy
