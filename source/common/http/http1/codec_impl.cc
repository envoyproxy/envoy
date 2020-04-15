#include "common/http/http1/codec_impl.h"

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"

#include "common/common/cleanup.h"
#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/http/exception.h"
#include "common/http/header_utility.h"
#include "common/http/headers.h"
#include "common/http/http1/header_formatter.h"
#include "common/http/utility.h"
#include "common/runtime/runtime_impl.h"

#include "absl/container/fixed_array.h"
#include "absl/strings/ascii.h"

namespace Envoy {
namespace Http {
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
};

struct Http1HeaderTypesValues {
  const absl::string_view Headers = "headers";
  const absl::string_view Trailers = "trailers";
};

// The following define special return values for http_parser callbacks. http_parser callbacks
// should return non-zero to indicate an error and halt execution. The one exception is
// on_headers_complete, where returning '1' will tell http_parser that it should not expect a body,
// and returning '2' from on_headers_complete will tell http_parser that it should not expect a body
// nor any further data on the connection.
constexpr int kCallbackError = -1;
constexpr int kNoBody = 1;
constexpr int kNoBodyOrData = 2;

using Http1ResponseCodeDetails = ConstSingleton<Http1ResponseCodeDetailValues>;
using Http1HeaderTypes = ConstSingleton<Http1HeaderTypesValues>;

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
      processing_100_continue_(false), is_response_to_head_request_(false),
      is_content_length_allowed_(true), header_key_formatter_(header_key_formatter) {
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
  processing_100_continue_ = true;
  encodeHeaders(headers, false);
  processing_100_continue_ = false;
}

void StreamEncoderImpl::encodeHeadersBase(const RequestOrResponseHeaderMap& headers,
                                          bool end_stream) {
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

    trailers.iterate(
        [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
          static_cast<StreamEncoderImpl*>(context)->encodeFormattedHeader(
              header.key().getStringView(), header.value().getStringView());
          return HeaderMap::Iterate::Continue;
        },
        this);

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

void StreamEncoderImpl::readDisable(bool disable) { connection_.readDisable(disable); }

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

  encodeHeadersBase(headers, end_stream);
}

static const char REQUEST_POSTFIX[] = " HTTP/1.1\r\n";

void RequestEncoderImpl::encodeHeaders(const RequestHeaderMap& headers, bool end_stream) {
  const HeaderEntry* method = headers.Method();
  const HeaderEntry* path = headers.Path();
  if (!method || !path) {
    throw CodecClientException(":method and :path must be specified");
  }
  if (method->value() == Headers::get().MethodValues.Head) {
    head_request_ = true;
  }
  if (Utility::isUpgrade(headers)) {
    upgrade_request_ = true;
  }
  connection_.copyToBuffer(method->value().getStringView().data(), method->value().size());
  connection_.addCharToBuffer(' ');
  connection_.copyToBuffer(path->value().getStringView().data(), path->value().size());
  connection_.copyToBuffer(REQUEST_POSTFIX, sizeof(REQUEST_POSTFIX) - 1);

  encodeHeadersBase(headers, end_stream);
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
      Envoy::StatusOr<int> statusor =
          static_cast<ConnectionImpl*>(parser->data)->onHeaderField(at, length);
      // Halt execution with a non-zero code if the callback returns an error.
      return statusor.ok() ? statusor.value() : kCallbackError;
    },
    [](http_parser* parser, const char* at, size_t length) -> int {
      static_cast<ConnectionImpl*>(parser->data)->onHeaderValue(at, length);
      return 0;
    },
    [](http_parser* parser) -> int {
      Envoy::StatusOr<int> statusor =
          static_cast<ConnectionImpl*>(parser->data)->onHeadersCompleteBase();
      // Halt execution with a non-zero code that is not 1 or 2. These have special meanings for
      // on_headers_complete.
      return statusor.ok() ? statusor.value() : kCallbackError;
    },
    [](http_parser* parser, const char* at, size_t length) -> int {
      static_cast<ConnectionImpl*>(parser->data)->bufferBody(at, length);
      return 0;
    },
    [](http_parser* parser) -> int {
      Envoy::StatusOr<int> statusor =
          static_cast<ConnectionImpl*>(parser->data)->onMessageCompleteBase();
      // Halt execution with a non-zero code if the callback returns an error.
      return statusor.ok() ? statusor.value() : kCallbackError;
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

ConnectionImpl::ConnectionImpl(Network::Connection& connection, Stats::Scope& stats,
                               http_parser_type type, uint32_t max_headers_kb,
                               const uint32_t max_headers_count,
                               HeaderKeyFormatterPtr&& header_key_formatter, bool enable_trailers)
    : connection_(connection), stats_{ALL_HTTP1_CODEC_STATS(POOL_COUNTER_PREFIX(stats, "http1."))},
      header_key_formatter_(std::move(header_key_formatter)), processing_trailers_(false),
      handling_upgrade_(false), reset_stream_called_(false), deferred_end_stream_headers_(false),
      strict_header_validation_(
          Runtime::runtimeFeatureEnabled("envoy.reloadable_features.strict_header_validation")),
      connection_header_sanitization_(Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.connection_header_sanitization")),
      enable_trailers_(enable_trailers),
      reject_unsupported_transfer_encodings_(Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.reject_unsupported_transfer_encodings")),
      dispatching_(false), output_buffer_([&]() -> void { this->onBelowLowWatermark(); },
                                          [&]() -> void { this->onAboveHighWatermark(); }),
      max_headers_kb_(max_headers_kb), max_headers_count_(max_headers_count) {
  output_buffer_.setWatermarks(connection.bufferLimit());
  http_parser_init(&parser_, type);
  parser_.data = this;
}

Envoy::StatusOr<int> ConnectionImpl::completeLastHeader() {
  ASSERT(dispatching_);
  ENVOY_CONN_LOG(trace, "completed header: key={} value={}", connection_,
                 current_header_field_.getStringView(), current_header_value_.getStringView());

  checkHeaderNameForUnderscores();
  auto& headers_or_trailers = headersOrTrailers();
  if (!current_header_field_.empty()) {
    current_header_field_.inlineTransform([](char c) { return absl::ascii_tolower(c); });
    headers_or_trailers.addViaMove(std::move(current_header_field_),
                                   std::move(current_header_value_));
  }

  // Check if the number of headers exceeds the limit.
  if (headers_or_trailers.size() > max_headers_count_) {
    error_code_ = Http::Code::RequestHeaderFieldsTooLarge;
    sendProtocolError(Http1ResponseCodeDetails::get().TooManyHeaders);
    const absl::string_view header_type =
        processing_trailers_ ? Http1HeaderTypes::get().Trailers : Http1HeaderTypes::get().Headers;
    codec_status_ = Http::codecProtocolError(absl::StrCat(header_type, " size exceeds limit"));
    return codec_status_;
  }

  header_parsing_state_ = HeaderParsingState::Field;
  ASSERT(current_header_field_.empty());
  ASSERT(current_header_value_.empty());
  // Successful exit-code.
  return 0;
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

Envoy::Http::Status ConnectionImpl::dispatch(Buffer::Instance& data) {
  ENVOY_CONN_LOG(trace, "parsing {} bytes", connection_, data.length());
  // Make sure that dispatching_ is set to false after dispatching, even when
  // ConnectionImpl::dispatch throws an exception.
  Cleanup cleanup([this]() { dispatching_ = false; });
  ASSERT(!dispatching_);
  ASSERT(buffered_body_.length() == 0);
  dispatching_ = true;

  if (maybeDirectDispatch(data)) {
    return absl::OkStatus();
  }

  // Always unpause before dispatch.
  http_parser_pause(&parser_, 0);

  ssize_t total_parsed = 0;
  if (data.length() > 0) {
    for (const Buffer::RawSlice& slice : data.getRawSlices()) {
      auto result = dispatchSlice(static_cast<const char*>(slice.mem_), slice.len_);
      if (!result.ok()) {
        // Return error status.
        return result.status();
      } else {
        total_parsed += result.value();
        if (HTTP_PARSER_ERRNO(&parser_) != HPE_OK) {
          // Parse errors trigger an exception in dispatchSlice or an error status so we are
          // guaranteed to be paused at this point.
          ASSERT(HTTP_PARSER_ERRNO(&parser_) == HPE_PAUSED);
          break;
        }
      }
    }
    dispatchBufferedBody();
  } else {
    auto result = dispatchSlice(nullptr, 0);
    if (!result.ok()) {
      return result.status();
    }
  }
  ASSERT(buffered_body_.length() == 0);

  ENVOY_CONN_LOG(trace, "parsed {} bytes", connection_, total_parsed);
  data.drain(total_parsed);

  // If an upgrade has been handled and there is body data or early upgrade
  // payload to send on, send it on.
  maybeDirectDispatch(data);
  return absl::OkStatus();
}

Envoy::StatusOr<ssize_t> ConnectionImpl::dispatchSlice(const char* slice, size_t len) {
  ASSERT(codec_status_.ok());
  ssize_t rc = http_parser_execute(&parser_, &settings_, slice, len);
  if (!codec_status_.ok()) {
    // Return early from error status.
    return codec_status_;
  }

  // Avoid overwriting the codec_status_ we set in the callbacks.
  if (HTTP_PARSER_ERRNO(&parser_) != HPE_OK && HTTP_PARSER_ERRNO(&parser_) != HPE_PAUSED &&
      codec_status_.ok()) {
    // TODO: We should replace the exception with setting the codec_status_ here.
    sendProtocolError(Http1ResponseCodeDetails::get().HttpCodecError);
    throw CodecProtocolException("http/1.1 protocol error: " +
                                 std::string(http_errno_name(HTTP_PARSER_ERRNO(&parser_))));
  }

  return rc;
}

Envoy::StatusOr<int> ConnectionImpl::onHeaderField(const char* data, size_t length) {
  ASSERT(dispatching_);
  // We previously already finished up the headers, these headers are
  // now trailers.
  if (header_parsing_state_ == HeaderParsingState::Done) {
    if (!enable_trailers_) {
      // Ignore trailers.
      return 0;
    }
    processing_trailers_ = true;
    header_parsing_state_ = HeaderParsingState::Field;
  }
  if (header_parsing_state_ == HeaderParsingState::Value) {
    auto statusor = completeLastHeader();
    if (!statusor.ok()) {
      // While we could return a non-zero exit code here, for clarity we return the invalid status.
      ASSERT(!codec_status_.ok());
      return statusor;
    }
  }

  current_header_field_.append(data, length);
  return 0;
}

void ConnectionImpl::onHeaderValue(const char* data, size_t length) {
  if (header_parsing_state_ == HeaderParsingState::Done && !enable_trailers_) {
    // Ignore trailers.
    return;
  }

  if (processing_trailers_) {
    maybeAllocTrailers();
  }

  // Work around a bug in http_parser where trailing whitespace is not trimmed
  // as the spec requires: https://tools.ietf.org/html/rfc7230#section-3.2.4
  const absl::string_view header_value = StringUtil::trim(absl::string_view(data, length));

  if (strict_header_validation_) {
    if (!Http::HeaderUtility::headerValueIsValid(header_value)) {
      ENVOY_CONN_LOG(debug, "invalid header value: {}", connection_, header_value);
      error_code_ = Http::Code::BadRequest;
      sendProtocolError(Http1ResponseCodeDetails::get().InvalidCharacters);
      throw CodecProtocolException("http/1.1 protocol error: header value contains invalid chars");
    }
  }

  header_parsing_state_ = HeaderParsingState::Value;
  current_header_value_.append(header_value.data(), header_value.length());

  const uint32_t total =
      current_header_field_.size() + current_header_value_.size() + headersOrTrailers().byteSize();
  if (total > (max_headers_kb_ * 1024)) {
    const absl::string_view header_type =
        processing_trailers_ ? Http1HeaderTypes::get().Trailers : Http1HeaderTypes::get().Headers;
    error_code_ = Http::Code::RequestHeaderFieldsTooLarge;
    sendProtocolError(Http1ResponseCodeDetails::get().HeadersTooLarge);
    throw CodecProtocolException(absl::StrCat(header_type, " size exceeds limit"));
  }
}

Envoy::StatusOr<int> ConnectionImpl::onHeadersCompleteBase() {
  ASSERT(dispatching_);
  ASSERT(!processing_trailers_);
  ENVOY_CONN_LOG(trace, "onHeadersCompleteBase", connection_);
  auto statusor = completeLastHeader();
  if (!statusor.ok()) {
    // An error occurred during completeLastHeader().
    ASSERT(!codec_status_.ok());
    return statusor;
  }

  if (!(parser_.http_major == 1 && parser_.http_minor == 1)) {
    // This is not necessarily true, but it's good enough since higher layers only care if this is
    // HTTP/1.1 or not.
    protocol_ = Protocol::Http10;
  }
  RequestOrResponseHeaderMap& request_or_response_headers = requestOrResponseHeaders();
  if (Utility::isUpgrade(request_or_response_headers) && upgradeAllowed()) {
    // Ignore h2c upgrade requests until we support them.
    // See https://github.com/envoyproxy/envoy/issues/7161 for details.
    if (request_or_response_headers.Upgrade() &&
        absl::EqualsIgnoreCase(request_or_response_headers.Upgrade()->value().getStringView(),
                               Http::Headers::get().UpgradeValues.H2c)) {
      ENVOY_CONN_LOG(trace, "removing unsupported h2c upgrade headers.", connection_);
      request_or_response_headers.removeUpgrade();
      if (request_or_response_headers.Connection()) {
        const auto& tokens_to_remove = caseUnorderdSetContainingUpgradeAndHttp2Settings();
        std::string new_value = StringUtil::removeTokens(
            request_or_response_headers.Connection()->value().getStringView(), ",",
            tokens_to_remove, ",");
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

  // Per https://tools.ietf.org/html/rfc7230#section-3.3.1 Envoy should reject
  // transfer-codings it does not understand.
  if (request_or_response_headers.TransferEncoding()) {
    const absl::string_view encoding =
        request_or_response_headers.TransferEncoding()->value().getStringView();
    if (reject_unsupported_transfer_encodings_ &&
        !absl::EqualsIgnoreCase(encoding, Headers::get().TransferEncodingValues.Chunked)) {
      error_code_ = Http::Code::NotImplemented;
      sendProtocolError(Http1ResponseCodeDetails::get().InvalidTransferEncoding);
      throw CodecProtocolException("http/1.1 protocol error: unsupported transfer encoding");
    }
  }

  int rc = onHeadersComplete();
  header_parsing_state_ = HeaderParsingState::Done;

  // Returning 2 informs http_parser to not expect a body or further data on this connection.
  return handling_upgrade_ ? kNoBodyOrData : rc;
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

Envoy::StatusOr<int> ConnectionImpl::onMessageCompleteBase() {
  ASSERT(dispatching_);
  ENVOY_CONN_LOG(trace, "message complete", connection_);

  dispatchBufferedBody();

  if (handling_upgrade_) {
    // If this is an upgrade request, swallow the onMessageComplete. The
    // upgrade payload will be treated as stream body.
    ASSERT(!deferred_end_stream_headers_);
    ENVOY_CONN_LOG(trace, "Pausing parser due to upgrade.", connection_);
    http_parser_pause(&parser_, 1);
    return 0;
  }

  // If true, this indicates we were processing trailers and must
  // move the last header into current_header_map_
  if (header_parsing_state_ == HeaderParsingState::Value) {
    auto statusor = completeLastHeader();
    if (!statusor.ok()) {
      ASSERT(!codec_status_.ok());
      return statusor;
    }
  }

  onMessageComplete();
  return 0;
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
    Network::Connection& connection, Stats::Scope& stats, ServerConnectionCallbacks& callbacks,
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
  if (!active_request.request_url_.getStringView().empty() &&
      (active_request.request_url_.getStringView()[0] == '/' ||
       ((method == HTTP_OPTIONS) && active_request.request_url_.getStringView()[0] == '*'))) {
    headers.addViaMove(std::move(path), std::move(active_request.request_url_));
    return;
  }

  // If absolute_urls and/or connect are not going be handled, copy the url and return.
  // This forces the behavior to be backwards compatible with the old codec behavior.
  if (!codec_settings_.allow_absolute_url_) {
    headers.addViaMove(std::move(path), std::move(active_request.request_url_));
    return;
  }

  if (is_connect) {
    headers.addViaMove(std::move(path), std::move(active_request.request_url_));
    return;
  }

  Utility::Url absolute_url;
  if (!absolute_url.initialize(active_request.request_url_.getStringView())) {
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
  headers.setHost(absolute_url.host_and_port());

  headers.setPath(absolute_url.path_and_query_params());
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
        absl::string_view header_value = headers->Connection()->value().getStringView();
        ENVOY_CONN_LOG(debug, "Invalid nominated headers in Connection: {}", connection_,
                       header_value);
        error_code_ = Http::Code::BadRequest;
        sendProtocolError(Http1ResponseCodeDetails::get().ConnectionHeaderSanitization);
        throw CodecProtocolException("Invalid nominated headers in Connection.");
      }
    }

    // Inform the response encoder about any HEAD method, so it can set content
    // length and transfer encoding headers correctly.
    active_request.response_encoder_.isResponseToHeadRequest(parser_.method == HTTP_HEAD);

    // Currently, CONNECT is not supported, however; http_parser_parse_url needs to know about
    // CONNECT
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
  if (active_request_.has_value()) {
    auto& active_request = active_request_.value();
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

void ServerConnectionImpl::sendProtocolError(absl::string_view details) {
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
      sendProtocolError(Http1ResponseCodeDetails::get().InvalidCharacters);
      stats_.requests_rejected_with_underscores_in_headers_.inc();
      throw CodecProtocolException("http/1.1 protocol error: header name contains underscores");
    }
  }
}

ClientConnectionImpl::ClientConnectionImpl(Network::Connection& connection, Stats::Scope& stats,
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

    if (parser_.status_code == 100) {
      // http-parser treats 100 continue headers as their own complete response.
      // Swallow the spurious onMessageComplete and continue processing.
      ignore_message_complete_for_100_continue_ = true;
      pending_response_.value().decoder_->decode100ContinueHeaders(std::move(headers));

      // Reset to ensure no information from the continue headers is used for the response headers
      // in case the callee does not move the headers out.
      headers_or_trailers_.emplace<ResponseHeaderMapPtr>(nullptr);
    } else if (cannotHaveBody()) {
      deferred_end_stream_headers_ = true;
    } else {
      pending_response_.value().decoder_->decodeHeaders(std::move(headers), false);
    }
  }

  // Here we deal with cases where the response cannot have a body, but http_parser does not deal
  // with it for us.
  return cannotHaveBody() ? kNoBody : 0;
}

bool ClientConnectionImpl::upgradeAllowed() const {
  if (pending_response_.has_value()) {
    return pending_response_->encoder_.upgrade_request_;
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
  if (ignore_message_complete_for_100_continue_) {
    ignore_message_complete_for_100_continue_ = false;
    return;
  }
  if (pending_response_.has_value()) {
    ASSERT(!pending_response_done_);
    // After calling decodeData() with end stream set to true, we should no longer be able to reset.
    PendingResponse& response = pending_response_.value();
    // Encoder is used as part of decode* calls later in this function so pending_response_ can not
    // be reset just yet. Preserve the state in pending_response_done_ instead.
    pending_response_done_ = true;

    // Streams are responsible for unwinding any outstanding readDisable(true)
    // calls done on the underlying connection as they are destroyed. As this is
    // the only place a HTTP/1 stream is destroyed where the Network::Connection is
    // reused, unwind any outstanding readDisable() calls here. Do this before we dispatch
    // end_stream in case the caller immediately reuses the connection.
    if (connection_.state() == Network::Connection::State::Open) {
      while (!connection_.readEnabled()) {
        connection_.readDisable(false);
      }
    }

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
} // namespace Http
} // namespace Envoy
