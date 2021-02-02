#include "common/http/http1/codec_impl.h"

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"

#include "common/common/cleanup.h"
#include "common/common/dump_state_utils.h"
#include "common/common/enum_to_int.h"
#include "common/common/scope_tracker.h"
#include "common/common/statusor.h"
#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/http/exception.h"
#include "common/http/header_utility.h"
#include "common/http/headers.h"
#include "common/http/http1/header_formatter.h"
#include "common/http/utility.h"
#include "common/runtime/runtime_features.h"

#include "absl/container/fixed_array.h"
#include "absl/strings/ascii.h"

namespace Envoy {
namespace Http {
namespace Http1 {
namespace {

// Changes or additions to details should be reflected in
// docs/root/configuration/http/http_conn_man/response_code_details_details.rst
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
  const absl::string_view ChunkedContentLength = "http1.content_length_and_chunked_not_allowed";
};

struct Http1HeaderTypesValues {
  const absl::string_view Headers = "headers";
  const absl::string_view Trailers = "trailers";
};

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
      connect_request_(false), is_tcp_tunneling_(false), is_response_to_head_request_(false),
      is_response_to_connect_request_(false), header_key_formatter_(header_key_formatter) {
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
  // With CONNECT or TCP tunneling, half-closing the connection is used to signal end stream.
  if (connect_request_ || is_tcp_tunneling_) {
    connection_.connection().close(Network::ConnectionCloseType::FlushWriteAndDelay);
  }
}

void ServerConnectionImpl::maybeAddSentinelBufferFragment(Buffer::Instance& output_buffer) {
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

Status ServerConnectionImpl::doFloodProtectionChecks() const {
  ASSERT(dispatching_);
  // Before processing another request, make sure that we are below the response flood protection
  // threshold.
  if (outbound_responses_ >= max_outbound_responses_) {
    ENVOY_CONN_LOG(trace, "error accepting request: too many pending responses queued",
                   connection_);
    stats_.response_flood_.inc();
    return bufferFloodError("Too many responses queued.");
  }
  return okStatus();
}

void ConnectionImpl::flushOutput(bool end_encode) {
  if (end_encode) {
    // If this is an HTTP response in ServerConnectionImpl, track outbound responses for flood
    // protection
    maybeAddSentinelBufferFragment(*output_buffer_);
  }
  connection().write(*output_buffer_, false);
  ASSERT(0UL == output_buffer_->length());
}

void ConnectionImpl::addToBuffer(absl::string_view data) { output_buffer_->add(data); }

void ConnectionImpl::addCharToBuffer(char c) { output_buffer_->add(&c, 1); }

void ConnectionImpl::addIntToBuffer(uint64_t i) { output_buffer_->add(absl::StrCat(i)); }

void ConnectionImpl::copyToBuffer(const char* data, uint64_t length) {
  output_buffer_->add(data, length);
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
  return connection_.connection().addressProvider().localAddress();
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

Status RequestEncoderImpl::encodeHeaders(const RequestHeaderMap& headers, bool end_stream) {
  // Required headers must be present. This can only happen by some erroneous processing after the
  // downstream codecs decode.
  RETURN_IF_ERROR(HeaderUtility::checkRequiredHeaders(headers));

  const HeaderEntry* method = headers.Method();
  const HeaderEntry* path = headers.Path();
  const HeaderEntry* host = headers.Host();
  bool is_connect = HeaderUtility::isConnect(headers);

  if (method->value() == Headers::get().MethodValues.Head) {
    head_request_ = true;
  } else if (method->value() == Headers::get().MethodValues.Connect) {
    disableChunkEncoding();
    connection_.connection().enableHalfClose(true);
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
  return okStatus();
}

int ConnectionImpl::setAndCheckCallbackStatus(Status&& status) {
  ASSERT(codec_status_.ok());
  codec_status_ = std::move(status);
  return codec_status_.ok() ? enumToSignedInt(HttpParserCode::Success)
                            : enumToSignedInt(HttpParserCode::Error);
}

int ConnectionImpl::setAndCheckCallbackStatusOr(Envoy::StatusOr<HttpParserCode>&& statusor) {
  ASSERT(codec_status_.ok());
  if (statusor.ok()) {
    return enumToSignedInt(statusor.value());
  } else {
    codec_status_ = std::move(statusor.status());
    return enumToSignedInt(HttpParserCode::Error);
  }
}

http_parser_settings ConnectionImpl::settings_{
    [](http_parser* parser) -> int {
      auto* conn_impl = static_cast<ConnectionImpl*>(parser->data);
      auto status = conn_impl->onMessageBeginBase();
      return conn_impl->setAndCheckCallbackStatus(std::move(status));
    },
    [](http_parser* parser, const char* at, size_t length) -> int {
      auto* conn_impl = static_cast<ConnectionImpl*>(parser->data);
      auto status = conn_impl->onUrl(at, length);
      return conn_impl->setAndCheckCallbackStatus(std::move(status));
    },
    nullptr, // on_status
    [](http_parser* parser, const char* at, size_t length) -> int {
      auto* conn_impl = static_cast<ConnectionImpl*>(parser->data);
      auto status = conn_impl->onHeaderField(at, length);
      return conn_impl->setAndCheckCallbackStatus(std::move(status));
    },
    [](http_parser* parser, const char* at, size_t length) -> int {
      auto* conn_impl = static_cast<ConnectionImpl*>(parser->data);
      auto status = conn_impl->onHeaderValue(at, length);
      return conn_impl->setAndCheckCallbackStatus(std::move(status));
    },
    [](http_parser* parser) -> int {
      auto* conn_impl = static_cast<ConnectionImpl*>(parser->data);
      auto statusor = conn_impl->onHeadersCompleteBase();
      return conn_impl->setAndCheckCallbackStatusOr(std::move(statusor));
    },
    [](http_parser* parser, const char* at, size_t length) -> int {
      static_cast<ConnectionImpl*>(parser->data)->bufferBody(at, length);
      return enumToSignedInt(HttpParserCode::Success);
    },
    [](http_parser* parser) -> int {
      auto* conn_impl = static_cast<ConnectionImpl*>(parser->data);
      auto status = conn_impl->onMessageCompleteBase();
      return conn_impl->setAndCheckCallbackStatus(std::move(status));
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
                               const Http1Settings& settings, http_parser_type type,
                               uint32_t max_headers_kb, const uint32_t max_headers_count,
                               HeaderKeyFormatterPtr&& header_key_formatter)
    : connection_(connection), stats_(stats), codec_settings_(settings),
      header_key_formatter_(std::move(header_key_formatter)), processing_trailers_(false),
      handling_upgrade_(false), reset_stream_called_(false), deferred_end_stream_headers_(false),
      strict_1xx_and_204_headers_(Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.strict_1xx_and_204_response_headers")),
      dispatching_(false), output_buffer_(connection.dispatcher().getWatermarkFactory().create(
                               [&]() -> void { this->onBelowLowWatermark(); },
                               [&]() -> void { this->onAboveHighWatermark(); },
                               []() -> void { /* TODO(adisuissa): Handle overflow watermark */ })),
      max_headers_kb_(max_headers_kb), max_headers_count_(max_headers_count) {
  output_buffer_->setWatermarks(connection.bufferLimit());
  http_parser_init(&parser_, type);
  parser_.allow_chunked_length = 1;
  parser_.data = this;
}

Status ConnectionImpl::completeLastHeader() {
  ASSERT(dispatching_);
  ENVOY_CONN_LOG(trace, "completed header: key={} value={}", connection_,
                 current_header_field_.getStringView(), current_header_value_.getStringView());

  RETURN_IF_ERROR(checkHeaderNameForUnderscores());
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
    RETURN_IF_ERROR(sendProtocolError(Http1ResponseCodeDetails::get().TooManyHeaders));
    const absl::string_view header_type =
        processing_trailers_ ? Http1HeaderTypes::get().Trailers : Http1HeaderTypes::get().Headers;
    return codecProtocolError(absl::StrCat(header_type, " size exceeds limit"));
  }

  header_parsing_state_ = HeaderParsingState::Field;
  ASSERT(current_header_field_.empty());
  ASSERT(current_header_value_.empty());
  return okStatus();
}

uint32_t ConnectionImpl::getHeadersSize() {
  return current_header_field_.size() + current_header_value_.size() +
         headersOrTrailers().byteSize();
}

Status ConnectionImpl::checkMaxHeadersSize() {
  const uint32_t total = getHeadersSize();
  if (total > (max_headers_kb_ * 1024)) {
    const absl::string_view header_type =
        processing_trailers_ ? Http1HeaderTypes::get().Trailers : Http1HeaderTypes::get().Headers;
    error_code_ = Http::Code::RequestHeaderFieldsTooLarge;
    RETURN_IF_ERROR(sendProtocolError(Http1ResponseCodeDetails::get().HeadersTooLarge));
    return codecProtocolError(absl::StrCat(header_type, " size exceeds limit"));
  }
  return okStatus();
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

Http::Status ClientConnectionImpl::dispatch(Buffer::Instance& data) {
  Http::Status status = ConnectionImpl::dispatch(data);
  if (status.ok() && data.length() > 0) {
    // The HTTP/1.1 codec pauses dispatch after a single response is complete. Extraneous data
    // after a response is complete indicates an error.
    return codecProtocolError("http/1.1 protocol error: extraneous data after response complete");
  }
  return status;
}

Http::Status ConnectionImpl::innerDispatch(Buffer::Instance& data) {
  // Add self to the Dispatcher's tracked object stack.
  ScopeTrackerScopeState scope(this, connection_.dispatcher());
  ENVOY_CONN_LOG(trace, "parsing {} bytes", connection_, data.length());
  // Make sure that dispatching_ is set to false after dispatching, even when
  // http_parser exits early with an error code.
  Cleanup cleanup([this]() { dispatching_ = false; });
  ASSERT(!dispatching_);
  ASSERT(codec_status_.ok());
  ASSERT(buffered_body_.length() == 0);

  dispatching_ = true;
  if (maybeDirectDispatch(data)) {
    return Http::okStatus();
  }

  // Always unpause before dispatch.
  http_parser_pause(&parser_, 0);

  ssize_t total_parsed = 0;
  if (data.length() > 0) {
    current_dispatching_buffer_ = &data;
    while (data.length() > 0) {
      auto slice = data.frontSlice();
      dispatching_slice_already_drained_ = false;
      auto statusor_parsed = dispatchSlice(static_cast<const char*>(slice.mem_), slice.len_);
      if (!statusor_parsed.ok()) {
        return statusor_parsed.status();
      }
      if (!dispatching_slice_already_drained_) {
        ASSERT(statusor_parsed.value() <= slice.len_);
        data.drain(statusor_parsed.value());
      }

      total_parsed += statusor_parsed.value();
      if (HTTP_PARSER_ERRNO(&parser_) != HPE_OK) {
        // Parse errors trigger an exception in dispatchSlice so we are guaranteed to be paused at
        // this point.
        ASSERT(HTTP_PARSER_ERRNO(&parser_) == HPE_PAUSED);
        break;
      }
    }
    current_dispatching_buffer_ = nullptr;
    dispatchBufferedBody();
  } else {
    auto result = dispatchSlice(nullptr, 0);
    if (!result.ok()) {
      return result.status();
    }
  }
  ASSERT(buffered_body_.length() == 0);

  ENVOY_CONN_LOG(trace, "parsed {} bytes", connection_, total_parsed);

  // If an upgrade has been handled and there is body data or early upgrade
  // payload to send on, send it on.
  maybeDirectDispatch(data);
  return Http::okStatus();
}

Envoy::StatusOr<size_t> ConnectionImpl::dispatchSlice(const char* slice, size_t len) {
  ASSERT(codec_status_.ok() && dispatching_);
  ssize_t rc = http_parser_execute(&parser_, &settings_, slice, len);
  if (!codec_status_.ok()) {
    return codec_status_;
  }
  if (HTTP_PARSER_ERRNO(&parser_) != HPE_OK && HTTP_PARSER_ERRNO(&parser_) != HPE_PAUSED) {
    RETURN_IF_ERROR(sendProtocolError(Http1ResponseCodeDetails::get().HttpCodecError));
    // Avoid overwriting the codec_status_ set in the callbacks.
    ASSERT(codec_status_.ok());
    codec_status_ = codecProtocolError(
        absl::StrCat("http/1.1 protocol error: ", http_errno_name(HTTP_PARSER_ERRNO(&parser_))));
    return codec_status_;
  }

  return rc;
}

Status ConnectionImpl::onHeaderField(const char* data, size_t length) {
  ASSERT(dispatching_);
  // We previously already finished up the headers, these headers are
  // now trailers.
  if (header_parsing_state_ == HeaderParsingState::Done) {
    if (!enableTrailers()) {
      // Ignore trailers.
      return okStatus();
    }
    processing_trailers_ = true;
    header_parsing_state_ = HeaderParsingState::Field;
    allocTrailers();
  }
  if (header_parsing_state_ == HeaderParsingState::Value) {
    RETURN_IF_ERROR(completeLastHeader());
  }

  current_header_field_.append(data, length);

  return checkMaxHeadersSize();
}

Status ConnectionImpl::onHeaderValue(const char* data, size_t length) {
  ASSERT(dispatching_);
  if (header_parsing_state_ == HeaderParsingState::Done && !enableTrailers()) {
    // Ignore trailers.
    return okStatus();
  }

  absl::string_view header_value{data, length};
  if (!Http::HeaderUtility::headerValueIsValid(header_value)) {
    ENVOY_CONN_LOG(debug, "invalid header value: {}", connection_, header_value);
    error_code_ = Http::Code::BadRequest;
    RETURN_IF_ERROR(sendProtocolError(Http1ResponseCodeDetails::get().InvalidCharacters));
    return codecProtocolError("http/1.1 protocol error: header value contains invalid chars");
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

  return checkMaxHeadersSize();
}

Envoy::StatusOr<ConnectionImpl::HttpParserCode> ConnectionImpl::onHeadersCompleteBase() {
  ASSERT(!processing_trailers_);
  ASSERT(dispatching_);
  ENVOY_CONN_LOG(trace, "onHeadersCompleteBase", connection_);
  RETURN_IF_ERROR(completeLastHeader());

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
        RETURN_IF_ERROR(sendProtocolError(Http1ResponseCodeDetails::get().BodyDisallowed));
        return codecProtocolError("http/1.1 protocol error: unsupported content length");
      }
    }
    ENVOY_CONN_LOG(trace, "codec entering upgrade mode for CONNECT request.", connection_);
    handling_upgrade_ = true;
  }

  // https://tools.ietf.org/html/rfc7230#section-3.3.3
  // If a message is received with both a Transfer-Encoding and a
  // Content-Length header field, the Transfer-Encoding overrides the
  // Content-Length. Such a message might indicate an attempt to
  // perform request smuggling (Section 9.5) or response splitting
  // (Section 9.4) and ought to be handled as an error. A sender MUST
  // remove the received Content-Length field prior to forwarding such
  // a message.

  // Reject message with Http::Code::BadRequest if both Transfer-Encoding and Content-Length
  // headers are present or if allowed by http1 codec settings and 'Transfer-Encoding'
  // is chunked - remove Content-Length and serve request.
  if (parser_.uses_transfer_encoding != 0 && request_or_response_headers.ContentLength()) {
    if ((parser_.flags & F_CHUNKED) && codec_settings_.allow_chunked_length_) {
      request_or_response_headers.removeContentLength();
    } else {
      error_code_ = Http::Code::BadRequest;
      RETURN_IF_ERROR(sendProtocolError(Http1ResponseCodeDetails::get().ChunkedContentLength));
      return codecProtocolError(
          "http/1.1 protocol error: both 'Content-Length' and 'Transfer-Encoding' are set.");
    }
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
      RETURN_IF_ERROR(sendProtocolError(Http1ResponseCodeDetails::get().InvalidTransferEncoding));
      return codecProtocolError("http/1.1 protocol error: unsupported transfer encoding");
    }
  }

  auto statusor = onHeadersComplete();
  if (!statusor.ok()) {
    RETURN_IF_ERROR(statusor.status());
  }

  header_parsing_state_ = HeaderParsingState::Done;

  // Returning HttpParserCode::NoBodyData informs http_parser to not expect a body or further data
  // on this connection.
  return handling_upgrade_ ? HttpParserCode::NoBodyData : statusor.value();
}

void ConnectionImpl::bufferBody(const char* data, size_t length) {
  auto slice = current_dispatching_buffer_->frontSlice();
  if (data == slice.mem_ && length == slice.len_) {
    buffered_body_.move(*current_dispatching_buffer_, length);
    dispatching_slice_already_drained_ = true;
  } else {
    buffered_body_.add(data, length);
  }
}

void ConnectionImpl::dispatchBufferedBody() {
  ASSERT(HTTP_PARSER_ERRNO(&parser_) == HPE_OK || HTTP_PARSER_ERRNO(&parser_) == HPE_PAUSED);
  ASSERT(codec_status_.ok());
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

Status ConnectionImpl::onMessageCompleteBase() {
  ENVOY_CONN_LOG(trace, "message complete", connection_);

  dispatchBufferedBody();

  if (handling_upgrade_) {
    // If this is an upgrade request, swallow the onMessageComplete. The
    // upgrade payload will be treated as stream body.
    ASSERT(!deferred_end_stream_headers_);
    ENVOY_CONN_LOG(trace, "Pausing parser due to upgrade.", connection_);
    http_parser_pause(&parser_, 1);
    return okStatus();
  }

  // If true, this indicates we were processing trailers and must
  // move the last header into current_header_map_
  if (header_parsing_state_ == HeaderParsingState::Value) {
    RETURN_IF_ERROR(completeLastHeader());
  }

  onMessageComplete();
  return okStatus();
}

Status ConnectionImpl::onMessageBeginBase() {
  ENVOY_CONN_LOG(trace, "message begin", connection_);
  // Make sure that if HTTP/1.0 and HTTP/1.1 requests share a connection Envoy correctly sets
  // protocol for each request. Envoy defaults to 1.1 but sets the protocol to 1.0 where applicable
  // in onHeadersCompleteBase
  protocol_ = Protocol::Http11;
  processing_trailers_ = false;
  header_parsing_state_ = HeaderParsingState::Field;
  allocHeaders();
  return onMessageBegin();
}

void ConnectionImpl::onResetStreamBase(StreamResetReason reason) {
  ASSERT(!reset_stream_called_);
  reset_stream_called_ = true;
  onResetStream(reason);
}

void ConnectionImpl::dumpState(std::ostream& os, int indent_level) const {
  const char* spaces = spacesForLevel(indent_level);
  os << spaces << "Http1::ConnectionImpl " << this << DUMP_MEMBER(dispatching_)
     << DUMP_MEMBER(dispatching_slice_already_drained_) << DUMP_MEMBER(reset_stream_called_)
     << DUMP_MEMBER(handling_upgrade_) << DUMP_MEMBER(deferred_end_stream_headers_)
     << DUMP_MEMBER(strict_1xx_and_204_headers_) << DUMP_MEMBER(processing_trailers_)
     << DUMP_MEMBER(buffered_body_.length());

  // Dump header parsing state, and any progress on headers.
  os << DUMP_MEMBER(header_parsing_state_);
  os << DUMP_MEMBER_AS(current_header_field_, current_header_field_.getStringView());
  os << DUMP_MEMBER_AS(current_header_value_, current_header_value_.getStringView());

  // Dump Child
  os << '\n';
  dumpAdditionalState(os, indent_level);

  // Dump the first slice of the dispatching buffer if not drained escaping
  // certain characters. We do this last as the slice could be rather large.
  if (current_dispatching_buffer_ == nullptr || dispatching_slice_already_drained_) {
    // Buffer is either null or already drained (in the body).
    // Use the macro for consistent formatting.
    os << DUMP_NULLABLE_MEMBER(current_dispatching_buffer_, "drained");
    return;
  } else {
    absl::string_view front_slice = [](Buffer::RawSlice slice) {
      return absl::string_view(static_cast<const char*>(slice.mem_), slice.len_);
    }(current_dispatching_buffer_->frontSlice());

    // Dump buffer data escaping \r, \n, \t, ", ', and \.
    // This is not the most performant implementation, but we're crashing and
    // cannot allocate memory.
    os << spaces << "current_dispatching_buffer_ front_slice length: " << front_slice.length()
       << " contents: \"";
    StringUtil::escapeToOstream(os, front_slice);
    os << "\"\n";
  }
}

void ServerConnectionImpl::dumpAdditionalState(std::ostream& os, int indent_level) const {
  const char* spaces = spacesForLevel(indent_level);
  os << DUMP_MEMBER_AS(active_request_.request_url_,
                       active_request_.has_value() &&
                               !active_request_.value().request_url_.getStringView().empty()
                           ? active_request_.value().request_url_.getStringView()
                           : "null");
  os << '\n';

  // Dump header map, it may be null if it was moved to the request, and
  // request_url.
  if (absl::holds_alternative<RequestHeaderMapPtr>(headers_or_trailers_)) {
    DUMP_DETAILS(absl::get<RequestHeaderMapPtr>(headers_or_trailers_));
  } else {
    DUMP_DETAILS(absl::get<RequestTrailerMapPtr>(headers_or_trailers_));
  }
}

void ClientConnectionImpl::dumpAdditionalState(std::ostream& os, int indent_level) const {
  const char* spaces = spacesForLevel(indent_level);
  // Dump header map, it may be null if it was moved to the request.
  if (absl::holds_alternative<ResponseHeaderMapPtr>(headers_or_trailers_)) {
    DUMP_DETAILS(absl::get<ResponseHeaderMapPtr>(headers_or_trailers_));
  } else {
    DUMP_DETAILS(absl::get<ResponseTrailerMapPtr>(headers_or_trailers_));
  }
}

ServerConnectionImpl::ServerConnectionImpl(
    Network::Connection& connection, CodecStats& stats, ServerConnectionCallbacks& callbacks,
    const Http1Settings& settings, uint32_t max_request_headers_kb,
    const uint32_t max_request_headers_count,
    envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
        headers_with_underscores_action)
    : ConnectionImpl(connection, stats, settings, HTTP_REQUEST, max_request_headers_kb,
                     max_request_headers_count, formatter(settings)),
      callbacks_(callbacks),
      response_buffer_releasor_([this](const Buffer::OwnedBufferFragmentImpl* fragment) {
        releaseOutboundResponse(fragment);
      }),
      // Pipelining is generally not well supported on the internet and has a series of dangerous
      // overflow bugs. As such we are disabling it for now, and removing this temporary override if
      // no one objects. If you use this integer to restore prior behavior, contact the
      // maintainer team as it will otherwise be removed entirely soon.
      max_outbound_responses_(
          Runtime::getInteger("envoy.do_not_use_going_away_max_http2_outbound_responses", 2)),
      headers_with_underscores_action_(headers_with_underscores_action) {}

uint32_t ServerConnectionImpl::getHeadersSize() {
  // Add in the size of the request URL if processing request headers.
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

Status ServerConnectionImpl::handlePath(RequestHeaderMap& headers, unsigned int method) {
  HeaderString path(Headers::get().Path);

  bool is_connect = (method == HTTP_CONNECT);

  // The url is relative or a wildcard when the method is OPTIONS. Nothing to do here.
  auto& active_request = active_request_.value();
  if (!is_connect && !active_request.request_url_.getStringView().empty() &&
      (active_request.request_url_.getStringView()[0] == '/' ||
       ((method == HTTP_OPTIONS) && active_request.request_url_.getStringView()[0] == '*'))) {
    headers.addViaMove(std::move(path), std::move(active_request.request_url_));
    return okStatus();
  }

  // If absolute_urls and/or connect are not going be handled, copy the url and return.
  // This forces the behavior to be backwards compatible with the old codec behavior.
  // CONNECT "urls" are actually host:port so look like absolute URLs to the above checks.
  // Absolute URLS in CONNECT requests will be rejected below by the URL class validation.
  if (!codec_settings_.allow_absolute_url_ && !is_connect) {
    headers.addViaMove(std::move(path), std::move(active_request.request_url_));
    return okStatus();
  }

  Utility::Url absolute_url;
  if (!absolute_url.initialize(active_request.request_url_.getStringView(), is_connect)) {
    RETURN_IF_ERROR(sendProtocolError(Http1ResponseCodeDetails::get().InvalidUrl));
    return codecProtocolError("http/1.1 protocol error: invalid url in request line");
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
  return okStatus();
}

Envoy::StatusOr<ConnectionImpl::HttpParserCode> ServerConnectionImpl::onHeadersComplete() {
  // Handle the case where response happens prior to request complete. It's up to upper layer code
  // to disconnect the connection but we shouldn't fire any more events since it doesn't make
  // sense.
  if (active_request_.has_value()) {
    auto& active_request = active_request_.value();
    auto& headers = absl::get<RequestHeaderMapPtr>(headers_or_trailers_);
    ENVOY_CONN_LOG(trace, "Server: onHeadersComplete size={}", connection_, headers->size());
    const char* method_string = http_method_str(static_cast<http_method>(parser_.method));

    if (!handling_upgrade_ && headers->Connection()) {
      // If we fail to sanitize the request, return a 400 to the client
      if (!Utility::sanitizeConnectionHeader(*headers)) {
        absl::string_view header_value = headers->getConnectionValue();
        ENVOY_CONN_LOG(debug, "Invalid nominated headers in Connection: {}", connection_,
                       header_value);
        error_code_ = Http::Code::BadRequest;
        RETURN_IF_ERROR(
            sendProtocolError(Http1ResponseCodeDetails::get().ConnectionHeaderSanitization));
        return codecProtocolError("Invalid nominated headers in Connection.");
      }
    }

    // Inform the response encoder about any HEAD method, so it can set content
    // length and transfer encoding headers correctly.
    active_request.response_encoder_.setIsResponseToHeadRequest(parser_.method == HTTP_HEAD);
    active_request.response_encoder_.setIsResponseToConnectRequest(parser_.method == HTTP_CONNECT);

    RETURN_IF_ERROR(handlePath(*headers, parser_.method));
    ASSERT(active_request.request_url_.empty());

    headers->setMethod(method_string);

    // Make sure the host is valid.
    auto details = HeaderUtility::requestHeadersValid(*headers);
    if (details.has_value()) {
      RETURN_IF_ERROR(sendProtocolError(details.value().get()));
      return codecProtocolError(
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

  return HttpParserCode::Success;
}

Status ServerConnectionImpl::onMessageBegin() {
  if (!resetStreamCalled()) {
    ASSERT(!active_request_.has_value());
    active_request_.emplace(*this, header_key_formatter_.get());
    auto& active_request = active_request_.value();
    if (resetStreamCalled()) {
      return codecClientError("cannot create new streams after calling reset");
    }
    active_request.request_decoder_ = &callbacks_.newStream(active_request.response_encoder_);

    // Check for pipelined request flood as we prepare to accept a new request.
    // Parse errors that happen prior to onMessageBegin result in stream termination, it is not
    // possible to overflow output buffers with early parse errors.
    RETURN_IF_ERROR(doFloodProtectionChecks());
  }
  return okStatus();
}

Status ServerConnectionImpl::onUrl(const char* data, size_t length) {
  if (active_request_.has_value()) {
    active_request_.value().request_url_.append(data, length);

    RETURN_IF_ERROR(checkMaxHeadersSize());
  }
  return okStatus();
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

Status ServerConnectionImpl::sendProtocolError(absl::string_view details) {
  // We do this here because we may get a protocol error before we have a logical stream.
  if (!active_request_.has_value()) {
    RETURN_IF_ERROR(onMessageBeginBase());
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
    active_request_->request_decoder_->sendLocalReply(is_grpc_request, error_code_,
                                                      CodeUtility::toString(error_code_), nullptr,
                                                      absl::nullopt, details);
  }
  return okStatus();
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

Status ServerConnectionImpl::checkHeaderNameForUnderscores() {
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
      RETURN_IF_ERROR(sendProtocolError(Http1ResponseCodeDetails::get().InvalidUnderscore));
      stats_.requests_rejected_with_underscores_in_headers_.inc();
      return codecProtocolError("http/1.1 protocol error: header name contains underscores");
    }
  }
  return okStatus();
}

ClientConnectionImpl::ClientConnectionImpl(Network::Connection& connection, CodecStats& stats,
                                           ConnectionCallbacks&, const Http1Settings& settings,
                                           const uint32_t max_response_headers_count)
    : ConnectionImpl(connection, stats, settings, HTTP_RESPONSE, MAX_RESPONSE_HEADERS_KB,
                     max_response_headers_count, formatter(settings)) {}

bool ClientConnectionImpl::cannotHaveBody() {
  if (pending_response_.has_value() && pending_response_.value().encoder_.headRequest()) {
    ASSERT(!pending_response_done_);
    return true;
  } else if (parser_.status_code == 204 || parser_.status_code == 304 ||
             (parser_.status_code >= 200 && parser_.content_length == 0 &&
              !(parser_.flags & F_CHUNKED))) {
    return true;
  } else {
    return false;
  }
}

RequestEncoder& ClientConnectionImpl::newStream(ResponseDecoder& response_decoder) {
  // If reads were disabled due to flow control, we expect reads to always be enabled again before
  // reusing this connection. This is done when the response is received.
  ASSERT(connection_.readEnabled());

  ASSERT(!pending_response_.has_value());
  ASSERT(pending_response_done_);
  pending_response_.emplace(*this, header_key_formatter_.get(), &response_decoder);
  pending_response_done_ = false;
  return pending_response_.value().encoder_;
}

Envoy::StatusOr<ConnectionImpl::HttpParserCode> ClientConnectionImpl::onHeadersComplete() {
  ENVOY_CONN_LOG(trace, "status_code {}", connection_, parser_.status_code);

  // Handle the case where the client is closing a kept alive connection (by sending a 408
  // with a 'Connection: close' header). In this case we just let response flush out followed
  // by the remote close.
  if (!pending_response_.has_value() && !resetStreamCalled()) {
    return prematureResponseError("", static_cast<Http::Code>(parser_.status_code));
  } else if (pending_response_.has_value()) {
    ASSERT(!pending_response_done_);
    auto& headers = absl::get<ResponseHeaderMapPtr>(headers_or_trailers_);
    ENVOY_CONN_LOG(trace, "Client: onHeadersComplete size={}", connection_, headers->size());
    headers->setStatus(parser_.status_code);

    if (parser_.status_code >= 200 && parser_.status_code < 300 &&
        pending_response_.value().encoder_.connectRequest()) {
      ENVOY_CONN_LOG(trace, "codec entering upgrade mode for CONNECT response.", connection_);
      handling_upgrade_ = true;
    }

    if (strict_1xx_and_204_headers_ && (parser_.status_code < 200 || parser_.status_code == 204)) {
      if (headers->TransferEncoding()) {
        RETURN_IF_ERROR(
            sendProtocolError(Http1ResponseCodeDetails::get().TransferEncodingNotAllowed));
        return codecProtocolError(
            "http/1.1 protocol error: transfer encoding not allowed in 1xx or 204");
      }

      if (headers->ContentLength()) {
        // Report a protocol error for non-zero Content-Length, but paper over zero Content-Length.
        if (headers->ContentLength()->value().getStringView() != "0") {
          RETURN_IF_ERROR(
              sendProtocolError(Http1ResponseCodeDetails::get().ContentLengthNotAllowed));
          return codecProtocolError(
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

  // Here we deal with cases where the response cannot have a body by returning
  // HttpParserCode::NoBody, but http_parser does not deal with it for us.
  return cannotHaveBody() ? HttpParserCode::NoBody : HttpParserCode::Success;
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

  // Pause the parser after a response is complete. Any remaining data indicates an error.
  http_parser_pause(&parser_, 1);
}

void ClientConnectionImpl::onResetStream(StreamResetReason reason) {
  // Only raise reset if we did not already dispatch a complete response.
  if (pending_response_.has_value() && !pending_response_done_) {
    pending_response_.value().encoder_.runResetCallbacks(reason);
    pending_response_done_ = true;
    pending_response_.reset();
  }
}

Status ClientConnectionImpl::sendProtocolError(absl::string_view details) {
  if (pending_response_.has_value()) {
    ASSERT(!pending_response_done_);
    pending_response_.value().encoder_.setDetails(details);
  }
  return okStatus();
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
