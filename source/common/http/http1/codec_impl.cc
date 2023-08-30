#include "source/common/http/http1/codec_impl.h"

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/optref.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"

#include "source/common/common/cleanup.h"
#include "source/common/common/dump_state_utils.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/scope_tracker.h"
#include "source/common/common/statusor.h"
#include "source/common/common/utility.h"
#include "source/common/grpc/common.h"
#include "source/common/http/exception.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/http1/balsa_parser.h"
#include "source/common/http/http1/header_formatter.h"
#include "source/common/http/http1/legacy_parser_impl.h"
#include "source/common/http/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/container/fixed_array.h"
#include "absl/strings/ascii.h"

namespace Envoy {
namespace Http {
namespace Http1 {
namespace {

// Changes or additions to details should be reflected in
// docs/root/configuration/http/http_conn_man/response_code_details.rst
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
  const absl::string_view HttpsInPlaintext = "http1.https_url_on_plaintext_connection";
  const absl::string_view InvalidScheme = "http1.invalid_scheme";
};

struct Http1HeaderTypesValues {
  const absl::string_view Headers = "headers";
  const absl::string_view Trailers = "trailers";
};

// Pipelining is generally not well supported on the internet and has a series of dangerous
// overflow bugs. As such Envoy disabled it.
static constexpr uint32_t kMaxOutboundResponses = 2;

using Http1ResponseCodeDetails = ConstSingleton<Http1ResponseCodeDetailValues>;
using Http1HeaderTypes = ConstSingleton<Http1HeaderTypesValues>;

const StringUtil::CaseUnorderedSet& caseUnorderdSetContainingUpgradeAndHttp2Settings() {
  CONSTRUCT_ON_FIRST_USE(StringUtil::CaseUnorderedSet,
                         Http::Headers::get().ConnectionValues.Upgrade,
                         Http::Headers::get().ConnectionValues.Http2Settings);
}

HeaderKeyFormatterConstPtr encodeOnlyFormatterFromSettings(const Http::Http1Settings& settings) {
  if (settings.header_key_format_ == Http1Settings::HeaderKeyFormat::ProperCase) {
    return std::make_unique<ProperCaseHeaderKeyFormatter>();
  }

  return nullptr;
}

StatefulHeaderKeyFormatterPtr statefulFormatterFromSettings(const Http::Http1Settings& settings) {
  if (settings.header_key_format_ == Http1Settings::HeaderKeyFormat::StatefulFormatter) {
    return settings.stateful_header_key_formatter_->create();
  }
  return nullptr;
}

constexpr size_t CRLF_SIZE = 2;

} // namespace

static constexpr absl::string_view CRLF = "\r\n";
// Last chunk as defined here https://tools.ietf.org/html/rfc7230#section-4.1
static constexpr absl::string_view LAST_CHUNK = "0\r\n";

static constexpr absl::string_view SPACE = " ";
static constexpr absl::string_view COLON_SPACE = ": ";

StreamEncoderImpl::StreamEncoderImpl(ConnectionImpl& connection,
                                     StreamInfo::BytesMeterSharedPtr&& bytes_meter)
    : connection_(connection), disable_chunk_encoding_(false), chunk_encoding_(true),
      connect_request_(false), is_tcp_tunneling_(false), is_response_to_head_request_(false),
      is_response_to_connect_request_(false), bytes_meter_(std::move(bytes_meter)) {
  if (!bytes_meter_) {
    bytes_meter_ = std::make_shared<StreamInfo::BytesMeter>();
  }
  if (connection_.connection().aboveHighWatermark()) {
    runHighWatermarkCallbacks();
  }
}

void StreamEncoderImpl::encodeHeader(absl::string_view key, absl::string_view value) {
  ASSERT(!key.empty());

  const uint64_t header_size = connection_.buffer().addFragments({key, COLON_SPACE, value, CRLF});

  bytes_meter_->addHeaderBytesSent(header_size);
}

void StreamEncoderImpl::encodeFormattedHeader(absl::string_view key, absl::string_view value,
                                              HeaderKeyFormatterOptConstRef formatter) {
  if (formatter.has_value()) {
    encodeHeader(formatter->format(key), value);
  } else {
    encodeHeader(key, value);
  }
}

void ResponseEncoderImpl::encode1xxHeaders(const ResponseHeaderMap& headers) {
  ASSERT(HeaderUtility::isSpecial1xx(headers));
  encodeHeaders(headers, false);
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.http1_allow_codec_error_response_after_1xx_headers")) {
    // Don't consider 100-continue responses as the actual response.
    started_response_ = false;
  }
}

void StreamEncoderImpl::encodeHeadersBase(const RequestOrResponseHeaderMap& headers,
                                          absl::optional<uint64_t> status, bool end_stream,
                                          bool bodiless_request) {
  HeaderKeyFormatterOptConstRef formatter(headers.formatter());
  if (!formatter.has_value()) {
    formatter = connection_.formatter();
  }

  const Http::HeaderValues& header_values = Http::Headers::get();
  bool saw_content_length = false;
  headers.iterate(
      [this, &header_values, formatter](const HeaderEntry& header) -> HeaderMap::Iterate {
        absl::string_view key_to_use = header.key().getStringView();
        uint32_t key_size_to_use = header.key().size();
        // Translate :authority -> host so that upper layers do not need to deal with this.
        if (key_size_to_use > 1 && key_to_use[0] == ':' && key_to_use[1] == 'a') {
          key_to_use = absl::string_view(header_values.HostLegacy.get());
          key_size_to_use = header_values.HostLegacy.get().size();
        }

        // Skip all headers starting with ':' that make it here.
        if (key_to_use[0] == ':') {
          return HeaderMap::Iterate::Continue;
        }

        encodeFormattedHeader(key_to_use, header.value().getStringView(), formatter);

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
    if (status && (*status < 200 || *status == 204)) {
      // For 1xx and 204 responses, do not send the chunked encoding header or enable chunked
      // encoding: https://tools.ietf.org/html/rfc7230#section-3.3.1
      chunk_encoding_ = false;
    } else if (status && *status == 304) {
      // For 304 response, since it should never have a body, we should not need to chunk_encode at
      // all.
      chunk_encoding_ = false;
    } else if (end_stream && !is_response_to_head_request_) {
      // If this is a headers-only stream, append an explicit "Content-Length: 0" unless it's a
      // response to a HEAD request.
      // For 204s and 1xx where content length is disallowed, don't append the content length but
      // also don't chunk encode.
      // Also do not add content length for requests which should not have a
      // body, per https://tools.ietf.org/html/rfc7230#section-3.3.2
      if (!status || (*status >= 200 && *status != 204)) {
        if (!bodiless_request) {
          encodeFormattedHeader(header_values.ContentLength.get(), "0", formatter);
        }
      }
      chunk_encoding_ = false;
    } else if (connection_.protocol() == Protocol::Http10) {
      chunk_encoding_ = false;
    } else {
      // For responses to connect requests, do not send the chunked encoding header:
      // https://tools.ietf.org/html/rfc7231#section-4.3.6.
      if (!is_response_to_connect_request_) {
        encodeFormattedHeader(header_values.TransferEncoding.get(),
                              header_values.TransferEncodingValues.Chunked, formatter);
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

  connection_.buffer().add(CRLF);

  if (end_stream) {
    endEncode();
  } else {
    flushOutput();
  }
}

void StreamEncoderImpl::encodeData(Buffer::Instance& data, bool end_stream) {
  // end_stream may be indicated with a zero length data buffer. If that is the case, so not
  // actually write the zero length buffer out.
  if (data.length() > 0) {
    if (chunk_encoding_) {
      std::string chunk_header = absl::StrCat(absl::Hex(data.length()), CRLF);
      connection_.buffer().add(std::move(chunk_header));
    }

    connection_.buffer().move(data);

    if (chunk_encoding_) {
      connection_.buffer().add(CRLF);
    }
  }

  if (end_stream) {
    endEncode();
  } else {
    flushOutput();
  }
}

void StreamEncoderImpl::flushOutput(bool end_encode) {
  auto encoded_bytes = connection_.flushOutput(end_encode);
  bytes_meter_->addWireBytesSent(encoded_bytes);
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

    // TODO(mattklein123): Wire up the formatter if someone actually asks for this (very unlikely).
    trailers.iterate([this](const HeaderEntry& header) -> HeaderMap::Iterate {
      encodeFormattedHeader(header.key().getStringView(), header.value().getStringView(),
                            HeaderKeyFormatterOptConstRef());
      return HeaderMap::Iterate::Continue;
    });

    connection_.buffer().add(CRLF);
  }

  flushOutput();
  notifyEncodeComplete();
}

void StreamEncoderImpl::encodeMetadata(const MetadataMapVector&) {
  connection_.stats().metadata_not_supported_error_.inc();
}

void StreamEncoderImpl::endEncode() {
  if (chunk_encoding_) {
    connection_.buffer().addFragments({LAST_CHUNK, CRLF});
  }

  flushOutput(true);
  notifyEncodeComplete();
  // With CONNECT or TCP tunneling, half-closing the connection is used to signal end stream so
  // don't delay that signal.
  if (connect_request_ || is_tcp_tunneling_) {
    connection_.connection().close(
        Network::ConnectionCloseType::FlushWrite,
        StreamInfo::LocalCloseReasons::get().CloseForConnectRequestOrTcpTunneling);
  }
}

void StreamEncoderImpl::notifyEncodeComplete() {
  if (codec_callbacks_) {
    codec_callbacks_->onCodecEncodeComplete();
  }
  connection_.onEncodeComplete();
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
  ASSERT(outbound_responses_ < kMaxOutboundResponses);
  outbound_responses_++;
}

Status ServerConnectionImpl::doFloodProtectionChecks() const {
  ASSERT(dispatching_);
  // Before processing another request, make sure that we are below the response flood protection
  // threshold.
  if (outbound_responses_ >= kMaxOutboundResponses) {
    ENVOY_CONN_LOG(trace, "error accepting request: too many pending responses queued",
                   connection_);
    stats_.response_flood_.inc();
    return bufferFloodError("Too many responses queued.");
  }
  return okStatus();
}

uint64_t ConnectionImpl::flushOutput(bool end_encode) {
  if (end_encode) {
    // If this is an HTTP response in ServerConnectionImpl, track outbound responses for flood
    // protection
    maybeAddSentinelBufferFragment(*output_buffer_);
  }
  const uint64_t bytes_encoded = output_buffer_->length();
  connection().write(*output_buffer_, false);
  ASSERT(0UL == output_buffer_->length());
  return bytes_encoded;
}

CodecEventCallbacks*
StreamEncoderImpl::registerCodecEventCallbacks(CodecEventCallbacks* codec_callbacks) {
  std::swap(codec_callbacks, codec_callbacks_);
  return codec_callbacks;
}

void StreamEncoderImpl::resetStream(StreamResetReason reason) {
  connection_.onResetStreamBase(reason);
}

void ResponseEncoderImpl::resetStream(StreamResetReason reason) {
  // Clear the downstream on the account since we're resetting the downstream.
  if (buffer_memory_account_) {
    buffer_memory_account_->clearDownstream();
  }

  // For H1, we use idleTimeouts to cancel streams unless there was an
  // explicit protocol error prior to sending a response to the downstream
  // in which case we send a local reply.
  // TODO(kbaichoo): If we want snappier resets of H1 streams we can
  //  1) Send local reply if no response data sent yet
  //  2) Invoke the idle timeout sooner to close underlying connection
  StreamEncoderImpl::resetStream(reason);
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

uint32_t StreamEncoderImpl::bufferLimit() const { return connection_.bufferLimit(); }

const Network::ConnectionInfoProvider& StreamEncoderImpl::connectionInfoProvider() {
  return connection_.connection().connectionInfoProvider();
}

static constexpr absl::string_view RESPONSE_PREFIX = "HTTP/1.1 ";
static constexpr absl::string_view HTTP_10_RESPONSE_PREFIX = "HTTP/1.0 ";

void ResponseEncoderImpl::encodeHeaders(const ResponseHeaderMap& headers, bool end_stream) {
  started_response_ = true;

  // The contract is that client codecs must ensure that :status is present and valid.
  ASSERT(headers.Status() != nullptr);
  uint64_t numeric_status = Utility::getResponseStatus(headers);

  absl::string_view response_prefix;
  if (connection_.protocol() == Protocol::Http10 && connection_.supportsHttp10()) {
    response_prefix = HTTP_10_RESPONSE_PREFIX;
  } else {
    response_prefix = RESPONSE_PREFIX;
  }

  StatefulHeaderKeyFormatterOptConstRef formatter(headers.formatter());

  absl::string_view reason_phrase;
  if (formatter.has_value() && !formatter->getReasonPhrase().empty()) {
    reason_phrase = formatter->getReasonPhrase();
  } else {
    const char* status_string = CodeUtility::toString(static_cast<Code>(numeric_status));
    uint32_t status_string_len = strlen(status_string);
    reason_phrase = {status_string, status_string_len};
  }

  connection_.buffer().addFragments(
      {response_prefix, absl::StrCat(numeric_status), SPACE, reason_phrase, CRLF});

  if (numeric_status >= 300) {
    // Don't do special CONNECT logic if the CONNECT was rejected.
    is_response_to_connect_request_ = false;
  }

  encodeHeadersBase(headers, absl::make_optional<uint64_t>(numeric_status), end_stream, false);
}

static constexpr absl::string_view REQUEST_POSTFIX = " HTTP/1.1\r\n";

Status RequestEncoderImpl::encodeHeaders(const RequestHeaderMap& headers, bool end_stream) {
#ifndef ENVOY_ENABLE_UHV
  // Headers are now validated by UHV before encoding by the codec. Two checks below are not needed
  // when UHV is enabled.
  //
  // Required headers must be present. This can only happen by some erroneous processing after the
  // downstream codecs decode.
  RETURN_IF_ERROR(HeaderUtility::checkRequiredRequestHeaders(headers));
  // Verify that a filter hasn't added an invalid header key or value.
  RETURN_IF_ERROR(HeaderUtility::checkValidRequestHeaders(headers));
#endif

  const HeaderEntry* method = headers.Method();
  const HeaderEntry* path = headers.Path();
  const HeaderEntry* host = headers.Host();
  bool is_connect = HeaderUtility::isConnect(headers);
  const Http::HeaderValues& header_values = Http::Headers::get();

  if (method->value() == header_values.MethodValues.Head) {
    head_request_ = true;
  } else if (method->value() == header_values.MethodValues.Connect) {
    disableChunkEncoding();
    connection_.connection().enableHalfClose(true);
    connect_request_ = true;
  }
  if (Utility::isUpgrade(headers)) {
    upgrade_request_ = true;
    // If the flag is flipped from true to false all outstanding upgrade requests that are waiting
    // for upstream connections will become invalid, as Envoy will add chunk encoding to the
    // protocol stream. This will likely cause the server to disconnect, since it will be unable to
    // parse the protocol.
    disableChunkEncoding();
  }

  if (connection_.sendFullyQualifiedUrl() && !is_connect) {
    const HeaderEntry* scheme = headers.Scheme();
    if (!scheme) {
      return absl::InvalidArgumentError(
          absl::StrCat("missing required header: ", Envoy::Http::Headers::get().Scheme.get()));
    }
    if (!host) {
      return absl::InvalidArgumentError(
          absl::StrCat("missing required header: ", Envoy::Http::Headers::get().Host.get()));
    }
    ASSERT(path);
    ASSERT(host);

    std::string url = absl::StrCat(scheme->value().getStringView(), "://",
                                   host->value().getStringView(), path->value().getStringView());
    ENVOY_CONN_LOG(trace, "Sending fully qualified URL: {}", connection_.connection(), url);
    connection_.buffer().addFragments(
        {method->value().getStringView(), SPACE, url, REQUEST_POSTFIX});
  } else {
    absl::string_view host_or_path_view;
    if (is_connect) {
      host_or_path_view = host->value().getStringView();
    } else {
      host_or_path_view = path->value().getStringView();
    }

    connection_.buffer().addFragments(
        {method->value().getStringView(), SPACE, host_or_path_view, REQUEST_POSTFIX});
  }

  encodeHeadersBase(headers, absl::nullopt, end_stream,
                    HeaderUtility::requestShouldHaveNoBody(headers));
  return okStatus();
}

CallbackResult ConnectionImpl::setAndCheckCallbackStatus(Status&& status) {
  ASSERT(codec_status_.ok());
  codec_status_ = std::move(status);
  return codec_status_.ok() ? CallbackResult::Success : CallbackResult::Error;
}

CallbackResult
ConnectionImpl::setAndCheckCallbackStatusOr(Envoy::StatusOr<CallbackResult>&& statusor) {
  ASSERT(codec_status_.ok());
  if (statusor.ok()) {
    return statusor.value();
  } else {
    codec_status_ = std::move(statusor.status());
    return CallbackResult::Error;
  }
}

ConnectionImpl::ConnectionImpl(Network::Connection& connection, CodecStats& stats,
                               const Http1Settings& settings, MessageType type,
                               uint32_t max_headers_kb, const uint32_t max_headers_count)
    : connection_(connection), stats_(stats), codec_settings_(settings),
      encode_only_header_key_formatter_(encodeOnlyFormatterFromSettings(settings)),
      processing_trailers_(false), handling_upgrade_(false), reset_stream_called_(false),
      deferred_end_stream_headers_(false), dispatching_(false), max_headers_kb_(max_headers_kb),
      max_headers_count_(max_headers_count) {
  if (codec_settings_.use_balsa_parser_) {
    parser_ = std::make_unique<BalsaParser>(type, this, max_headers_kb_ * 1024, enableTrailers(),
                                            codec_settings_.allow_custom_methods_);
  } else {
    parser_ = std::make_unique<LegacyHttpParserImpl>(type, this);
  }
}

Status ConnectionImpl::completeCurrentHeader() {
  ASSERT(dispatching_);
  ENVOY_CONN_LOG(trace, "completed header: key={} value={}", connection_,
                 current_header_field_.getStringView(), current_header_value_.getStringView());
  auto& headers_or_trailers = headersOrTrailers();

  // Account for ":" and "\r\n" bytes between the header key value pair.
  getBytesMeter().addHeaderBytesReceived(CRLF_SIZE + 1);

  // TODO(10646): Switch to use HeaderUtility::checkHeaderNameForUnderscores().
  RETURN_IF_ERROR(checkHeaderNameForUnderscores());
  if (!current_header_field_.empty()) {
    // Strip trailing whitespace of the current header value if any. Leading whitespace was trimmed
    // in ConnectionImpl::onHeaderValue. http_parser does not strip leading or trailing whitespace
    // as the spec requires: https://tools.ietf.org/html/rfc7230#section-3.2.4
    current_header_value_.rtrim();

    // If there is a stateful formatter installed, remember the original header key before
    // converting to lower case.
    auto formatter = headers_or_trailers.formatter();
    if (formatter.has_value()) {
      formatter->processKey(current_header_field_.getStringView());
    }
    current_header_field_.inlineTransform([](char c) { return absl::ascii_tolower(c); });

    headers_or_trailers.addViaMove(std::move(current_header_field_),
                                   std::move(current_header_value_));
  }

  // Check if the number of headers exceeds the limit.
  if (headers_or_trailers.size() > max_headers_count_) {
    error_code_ = Http::Code::RequestHeaderFieldsTooLarge;
    RETURN_IF_ERROR(sendProtocolError(Http1ResponseCodeDetails::get().TooManyHeaders));
    const absl::string_view header_type =
        processing_trailers_ ? Http1HeaderTypes::get().Trailers : Http1HeaderTypes::get().Headers;
    return codecProtocolError(
        absl::StrCat("http/1.1 protocol error: ", header_type, " count exceeds limit"));
  }

  header_parsing_state_ = HeaderParsingState::Field;
  ASSERT(current_header_field_.empty());
  ASSERT(current_header_value_.empty());
  return okStatus();
}

Status ConnectionImpl::onMessageBeginImpl() {
  ENVOY_CONN_LOG(trace, "message begin", connection_);
  // Make sure that if HTTP/1.0 and HTTP/1.1 requests share a connection Envoy correctly sets
  // protocol for each request. Envoy defaults to 1.1 but sets the protocol to 1.0 where applicable
  // in onHeadersCompleteBase
  protocol_ = Protocol::Http11;
  processing_trailers_ = false;
  header_parsing_state_ = HeaderParsingState::Field;
  allocHeaders(statefulFormatterFromSettings(codec_settings_));
  return onMessageBeginBase();
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
    return codecProtocolError(
        absl::StrCat("http/1.1 protocol error: ", header_type, " size exceeds limit"));
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

void ConnectionImpl::onDispatch(const Buffer::Instance& data) {
  getBytesMeter().addWireBytesReceived(data.length());
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

Http::Status ConnectionImpl::dispatch(Buffer::Instance& data) {
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
  onDispatch(data);
  if (maybeDirectDispatch(data)) {
    return Http::okStatus();
  }

  // Always resume before dispatch.
  parser_->resume();

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
      if (parser_->getStatus() != ParserStatus::Ok) {
        // Parse errors trigger an exception in dispatchSlice so we are guaranteed to be paused at
        // this point.
        ASSERT(parser_->getStatus() == ParserStatus::Paused);
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
  const size_t nread = parser_->execute(slice, len);
  if (!codec_status_.ok()) {
    return codec_status_;
  }

  const ParserStatus status = parser_->getStatus();
  if (status != ParserStatus::Ok && status != ParserStatus::Paused) {
    absl::string_view error = Http1ResponseCodeDetails::get().HttpCodecError;
    if (codec_settings_.use_balsa_parser_) {
      if (parser_->errorMessage() == "headers size exceeds limit" ||
          parser_->errorMessage() == "trailers size exceeds limit") {
        error_code_ = Http::Code::RequestHeaderFieldsTooLarge;
        error = Http1ResponseCodeDetails::get().HeadersTooLarge;
      } else if (parser_->errorMessage() == "header value contains invalid chars") {
        error = Http1ResponseCodeDetails::get().InvalidCharacters;
      }
    }
    RETURN_IF_ERROR(sendProtocolError(error));
    // Avoid overwriting the codec_status_ set in the callbacks.
    ASSERT(codec_status_.ok());
    codec_status_ =
        codecProtocolError(absl::StrCat("http/1.1 protocol error: ", parser_->errorMessage()));
    return codec_status_;
  }

  return nread;
}

CallbackResult ConnectionImpl::onMessageBegin() {
  return setAndCheckCallbackStatus(onMessageBeginImpl());
}

CallbackResult ConnectionImpl::onUrl(const char* data, size_t length) {
  return setAndCheckCallbackStatus(onUrlBase(data, length));
}

CallbackResult ConnectionImpl::onStatus(const char* data, size_t length) {
  return setAndCheckCallbackStatus(onStatusBase(data, length));
}

CallbackResult ConnectionImpl::onHeaderField(const char* data, size_t length) {
  return setAndCheckCallbackStatus(onHeaderFieldImpl(data, length));
}

CallbackResult ConnectionImpl::onHeaderValue(const char* data, size_t length) {
  return setAndCheckCallbackStatus(onHeaderValueImpl(data, length));
}

CallbackResult ConnectionImpl::onHeadersComplete() {
  return setAndCheckCallbackStatusOr(onHeadersCompleteImpl());
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

CallbackResult ConnectionImpl::onMessageComplete() {
  return setAndCheckCallbackStatusOr(onMessageCompleteImpl());
}

void ConnectionImpl::onChunkHeader(bool is_final_chunk) {
  if (is_final_chunk) {
    // Dispatch body before parsing trailers, so body ends up dispatched even if an error is found
    // while processing trailers.
    dispatchBufferedBody();
  }
}

Status ConnectionImpl::onHeaderFieldImpl(const char* data, size_t length) {
  ASSERT(dispatching_);

  getBytesMeter().addHeaderBytesReceived(length);

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
    RETURN_IF_ERROR(completeCurrentHeader());
  }

  current_header_field_.append(data, length);

  return checkMaxHeadersSize();
}

Status ConnectionImpl::onHeaderValueImpl(const char* data, size_t length) {
  ASSERT(dispatching_);

  getBytesMeter().addHeaderBytesReceived(length);

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
    // ConnectionImpl::completeCurrentHeader. http_parser does not strip leading or trailing
    // whitespace as the spec requires: https://tools.ietf.org/html/rfc7230#section-3.2.4 .
    header_value = StringUtil::ltrim(header_value);
  }
  current_header_value_.append(header_value.data(), header_value.length());

  return checkMaxHeadersSize();
}

StatusOr<CallbackResult> ConnectionImpl::onHeadersCompleteImpl() {
  ASSERT(!processing_trailers_);
  ASSERT(dispatching_);
  ENVOY_CONN_LOG(trace, "onHeadersCompleteImpl", connection_);
  RETURN_IF_ERROR(completeCurrentHeader());

  if (!parser_->isHttp11()) {
    // This is not necessarily true, but it's good enough since higher layers only care if this is
    // HTTP/1.1 or not.
    protocol_ = Protocol::Http10;
  }
  RequestOrResponseHeaderMap& request_or_response_headers = requestOrResponseHeaders();
  const Http::HeaderValues& header_values = Http::Headers::get();
  if (Utility::isUpgrade(request_or_response_headers) && upgradeAllowed()) {
    // Ignore h2c upgrade requests until we support them.
    // See https://github.com/envoyproxy/envoy/issues/7161 for details.
    if (absl::EqualsIgnoreCase(request_or_response_headers.getUpgradeValue(),
                               header_values.UpgradeValues.H2c)) {
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
      request_or_response_headers.remove(header_values.Http2Settings);
    } else {
      ENVOY_CONN_LOG(trace, "codec entering upgrade mode.", connection_);
      handling_upgrade_ = true;
    }
  }
  if (parser_->methodName() == header_values.MethodValues.Connect) {
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

#ifndef ENVOY_ENABLE_UHV
  // This check is moved into default header validator.
  // TODO(yanavlasov): use runtime override here when UHV is moved into the main build

  // Reject message with Http::Code::BadRequest if both Transfer-Encoding and Content-Length
  // headers are present or if allowed by http1 codec settings and 'Transfer-Encoding'
  // is chunked - remove Content-Length and serve request.
  if (parser_->hasTransferEncoding() != 0 && request_or_response_headers.ContentLength()) {
    if (parser_->isChunked() && codec_settings_.allow_chunked_length_) {
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
    if (!absl::EqualsIgnoreCase(encoding, header_values.TransferEncodingValues.Chunked) ||
        parser_->methodName() == header_values.MethodValues.Connect) {
      error_code_ = Http::Code::NotImplemented;
      RETURN_IF_ERROR(sendProtocolError(Http1ResponseCodeDetails::get().InvalidTransferEncoding));
      return codecProtocolError("http/1.1 protocol error: unsupported transfer encoding");
    }
  }
#endif

  auto statusor = onHeadersCompleteBase();
  if (!statusor.ok()) {
    RETURN_IF_ERROR(statusor.status());
  }

  header_parsing_state_ = HeaderParsingState::Done;

  // Returning CallbackResult::NoBodyData informs http_parser to not expect a body or further data
  // on this connection.
  return handling_upgrade_ ? CallbackResult::NoBodyData : statusor.value();
}

StatusOr<CallbackResult> ConnectionImpl::onMessageCompleteImpl() {
  ENVOY_CONN_LOG(trace, "message complete", connection_);

  dispatchBufferedBody();

  if (handling_upgrade_) {
    // If this is an upgrade request, swallow the onMessageComplete. The
    // upgrade payload will be treated as stream body.
    ASSERT(!deferred_end_stream_headers_);
    ENVOY_CONN_LOG(trace, "Pausing parser due to upgrade.", connection_);
    return parser_->pause();
  }

  // If true, this indicates we were processing trailers and must
  // move the last header into current_header_map_
  if (header_parsing_state_ == HeaderParsingState::Value) {
    RETURN_IF_ERROR(completeCurrentHeader());
  }

  return onMessageCompleteBase();
}

void ConnectionImpl::dispatchBufferedBody() {
  ASSERT(parser_->getStatus() == ParserStatus::Ok || parser_->getStatus() == ParserStatus::Paused);
  ASSERT(codec_status_.ok());
  if (buffered_body_.length() > 0) {
    onBody(buffered_body_);
    buffered_body_.drain(buffered_body_.length());
  }
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
     << DUMP_MEMBER(processing_trailers_) << DUMP_MEMBER(buffered_body_.length());

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

  DUMP_DETAILS(active_request_);
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

  // Dump the associated request.
  os << spaces << "Dumping corresponding downstream request:";
  if (pending_response_.has_value()) {
    os << '\n';
    const ResponseDecoder* decoder = pending_response_.value().decoder_;
    DUMP_DETAILS(decoder);
  } else {
    os << " null\n";
  }
}

ServerConnectionImpl::ServerConnectionImpl(
    Network::Connection& connection, CodecStats& stats, ServerConnectionCallbacks& callbacks,
    const Http1Settings& settings, uint32_t max_request_headers_kb,
    const uint32_t max_request_headers_count,
    envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
        headers_with_underscores_action,
    Server::OverloadManager& overload_manager)
    : ConnectionImpl(connection, stats, settings, MessageType::Request, max_request_headers_kb,
                     max_request_headers_count),
      callbacks_(callbacks),
      response_buffer_releasor_([this](const Buffer::OwnedBufferFragmentImpl* fragment) {
        releaseOutboundResponse(fragment);
      }),
      owned_output_buffer_(connection.dispatcher().getWatermarkFactory().createBuffer(
          [&]() -> void { this->onBelowLowWatermark(); },
          [&]() -> void { this->onAboveHighWatermark(); },
          []() -> void { /* TODO(adisuissa): handle overflow watermark */ })),
      headers_with_underscores_action_(headers_with_underscores_action),
      abort_dispatch_(
          overload_manager.getLoadShedPoint("envoy.load_shed_points.http1_server_abort_dispatch")) {
  ENVOY_LOG_ONCE_IF(trace, abort_dispatch_ == nullptr,
                    "LoadShedPoint envoy.load_shed_points.http1_server_abort_dispatch is not "
                    "found. Is it configured?");
  owned_output_buffer_->setWatermarks(connection.bufferLimit());
  // Inform parent
  output_buffer_ = owned_output_buffer_.get();
}

uint32_t ServerConnectionImpl::getHeadersSize() {
  // Add in the size of the request URL if processing request headers.
  const uint32_t url_size =
      (!processing_trailers_ && active_request_) ? active_request_->request_url_.size() : 0;
  return url_size + ConnectionImpl::getHeadersSize();
}

void ServerConnectionImpl::onEncodeComplete() {
  if (active_request_->remote_complete_) {
    // Only do this if remote is complete. If we are replying before the request is complete the
    // only logical thing to do is for higher level code to reset() / close the connection so we
    // leave the request around so that it can fire reset callbacks.
    connection_.dispatcher().deferredDelete(std::move(active_request_));
  }
}

Status ServerConnectionImpl::handlePath(RequestHeaderMap& headers, absl::string_view method) {
  const Http::HeaderValues& header_values = Http::Headers::get();
  HeaderString path(header_values.Path);

  bool is_connect = (method == header_values.MethodValues.Connect);

  // The url is relative or a wildcard when the method is OPTIONS. Nothing to do here.
  if (!is_connect && !active_request_->request_url_.getStringView().empty() &&
      (active_request_->request_url_.getStringView()[0] == '/' ||
       (method == header_values.MethodValues.Options &&
        active_request_->request_url_.getStringView()[0] == '*'))) {
    headers.addViaMove(std::move(path), std::move(active_request_->request_url_));
    return okStatus();
  }

  // If absolute_urls and/or connect are not going be handled, copy the url and return.
  // This forces the behavior to be backwards compatible with the old codec behavior.
  // CONNECT "urls" are actually host:port so look like absolute URLs to the above checks.
  // Absolute URLS in CONNECT requests will be rejected below by the URL class validation.

  /**
   * @param scheme the scheme to validate
   * @return bool true if the scheme is http.
   */
  if (!codec_settings_.allow_absolute_url_ && !is_connect) {
    headers.addViaMove(std::move(path), std::move(active_request_->request_url_));
    return okStatus();
  }

  Utility::Url absolute_url;
  if (!absolute_url.initialize(active_request_->request_url_.getStringView(), is_connect)) {
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
  // Add the scheme and validate to ensure no https://
  // requests are accepted over unencrypted connections by front-line Envoys.
  if (!is_connect) {
    if (Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.allow_absolute_url_with_mixed_scheme")) {
      headers.setScheme(absl::AsciiStrToLower(absolute_url.scheme()));
    } else {
      headers.setScheme(absolute_url.scheme());
    }
    if (!Utility::schemeIsValid(headers.getSchemeValue())) {
      RETURN_IF_ERROR(sendProtocolError(Http1ResponseCodeDetails::get().InvalidScheme));
      return codecProtocolError("http/1.1 protocol error: invalid scheme");
    }
    if (codec_settings_.validate_scheme_ && Utility::schemeIsHttps(absolute_url.scheme()) &&
        !connection().ssl()) {
      error_code_ = Http::Code::Forbidden;
      RETURN_IF_ERROR(sendProtocolError(Http1ResponseCodeDetails::get().HttpsInPlaintext));
      return codecProtocolError("http/1.1 protocol error: https in the clear");
    }
  }

  if (!absolute_url.pathAndQueryParams().empty()) {
    headers.setPath(absolute_url.pathAndQueryParams());
  }
  active_request_->request_url_.clear();
  return okStatus();
}

Status ServerConnectionImpl::checkProtocolVersion(RequestHeaderMap& headers) {
  if (protocol() == Protocol::Http10) {
    // Assume this is HTTP/1.0. This is fine for HTTP/0.9 but this code will also affect any
    // requests with non-standard version numbers (0.9, 1.3), basically anything which is not
    // HTTP/1.1.
    //
    // The protocol may have shifted in the HTTP/1.0 case so reset it.
    if (!codec_settings_.accept_http_10_) {
      // Send "Upgrade Required" if HTTP/1.0 support is not explicitly configured on.
      error_code_ = Http::Code::UpgradeRequired;
      RETURN_IF_ERROR(sendProtocolError(StreamInfo::ResponseCodeDetails::get().LowVersion));
      return codecProtocolError("Upgrade required for HTTP/1.0 or HTTP/0.9");
    }
    if (!headers.Host() && !codec_settings_.default_host_for_http_10_.empty()) {
      // Add a default host if configured to do so.
      headers.setHost(codec_settings_.default_host_for_http_10_);
    }
  }
  return okStatus();
}

Envoy::StatusOr<CallbackResult> ServerConnectionImpl::onHeadersCompleteBase() {
  // Handle the case where response happens prior to request complete. It's up to upper layer code
  // to disconnect the connection but we shouldn't fire any more events since it doesn't make
  // sense.
  if (active_request_) {
    auto& headers = absl::get<RequestHeaderMapPtr>(headers_or_trailers_);
    ENVOY_CONN_LOG(trace, "Server: onHeadersComplete size={}", connection_, headers->size());

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
    const Http::HeaderValues& header_values = Http::Headers::get();
    active_request_->response_encoder_.setIsResponseToHeadRequest(parser_->methodName() ==
                                                                  header_values.MethodValues.Head);
    active_request_->response_encoder_.setIsResponseToConnectRequest(
        parser_->methodName() == header_values.MethodValues.Connect);

    RETURN_IF_ERROR(handlePath(*headers, parser_->methodName()));
    ASSERT(active_request_->request_url_.empty());

    headers->setMethod(parser_->methodName());
    RETURN_IF_ERROR(checkProtocolVersion(*headers));

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
    if (parser_->isChunked() ||
        (parser_->contentLength().has_value() && parser_->contentLength().value() > 0) ||
        handling_upgrade_) {
      active_request_->request_decoder_->decodeHeaders(std::move(headers), false);

      // If the connection has been closed (or is closing) after decoding headers, pause the parser
      // so we return control to the caller.
      if (connection_.state() != Network::Connection::State::Open) {
        return parser_->pause();
      }
    } else {
      deferred_end_stream_headers_ = true;
    }
  }

  return CallbackResult::Success;
}

Status ServerConnectionImpl::onMessageBeginBase() {
  if (!resetStreamCalled()) {
    ASSERT(active_request_ == nullptr);
    active_request_ = std::make_unique<ActiveRequest>(*this, std::move(bytes_meter_before_stream_));
    active_request_->request_decoder_ = &callbacks_.newStream(active_request_->response_encoder_);

    // Check for pipelined request flood as we prepare to accept a new request.
    // Parse errors that happen prior to onMessageBegin result in stream termination, it is not
    // possible to overflow output buffers with early parse errors.
    RETURN_IF_ERROR(doFloodProtectionChecks());
  }
  return okStatus();
}

Status ServerConnectionImpl::onUrlBase(const char* data, size_t length) {
  if (active_request_) {
    active_request_->request_url_.append(data, length);

    RETURN_IF_ERROR(checkMaxHeadersSize());
  }

  return okStatus();
}

void ServerConnectionImpl::onBody(Buffer::Instance& data) {
  ASSERT(!deferred_end_stream_headers_);
  if (active_request_) {
    ENVOY_CONN_LOG(trace, "body size={}", connection_, data.length());
    active_request_->request_decoder_->decodeData(data, false);
  }
}

Http::Status ServerConnectionImpl::dispatch(Buffer::Instance& data) {
  if (abort_dispatch_ != nullptr && abort_dispatch_->shouldShedLoad()) {
    RETURN_IF_ERROR(sendOverloadError());
    return envoyOverloadError("Aborting Server Dispatch");
  }

  if (active_request_ != nullptr && active_request_->remote_complete_) {
    // Eagerly read disable the connection if the downstream is sending pipelined requests as we
    // serially process them. Reading from the connection will be re-enabled after the active
    // request is completed.
    active_request_->response_encoder_.readDisable(true);
    return okStatus();
  }

  Http::Status status = ConnectionImpl::dispatch(data);

  if (active_request_ != nullptr && active_request_->remote_complete_) {
    // Read disable the connection if the downstream is sending additional data while we are working
    // on an existing request. Reading from the connection will be re-enabled after the active
    // request is completed.
    if (data.length() > 0) {
      active_request_->response_encoder_.readDisable(true);
    }
  }
  return status;
}

CallbackResult ServerConnectionImpl::onMessageCompleteBase() {
  ASSERT(!handling_upgrade_);
  if (active_request_) {

    // The request_decoder should be non-null after we've called the newStream on callbacks.
    ASSERT(active_request_->request_decoder_);
    active_request_->remote_complete_ = true;

    if (deferred_end_stream_headers_) {
      active_request_->request_decoder_->decodeHeaders(
          std::move(absl::get<RequestHeaderMapPtr>(headers_or_trailers_)), true);
      deferred_end_stream_headers_ = false;
    } else if (processing_trailers_) {
      active_request_->request_decoder_->decodeTrailers(
          std::move(absl::get<RequestTrailerMapPtr>(headers_or_trailers_)));
    } else {
      Buffer::OwnedImpl buffer;
      active_request_->request_decoder_->decodeData(buffer, true);
    }

    // Reset to ensure no information from one requests persists to the next.
    headers_or_trailers_.emplace<RequestHeaderMapPtr>(nullptr);
  }

  // Always pause the parser so that the calling code can process 1 request at a time and apply
  // back pressure. However this means that the calling code needs to detect if there is more data
  // in the buffer and dispatch it again.
  return parser_->pause();
}

void ServerConnectionImpl::onResetStream(StreamResetReason reason) {
  if (active_request_) {
    active_request_->response_encoder_.runResetCallbacks(reason);
    connection_.dispatcher().deferredDelete(std::move(active_request_));
  }
}

Status ServerConnectionImpl::sendOverloadError() {
  const bool latched_dispatching = dispatching_;

  // The codec might be in the early stages of server dispatching where this isn't yet
  // flipped to true.
  dispatching_ = true;
  error_code_ = Http::Code::InternalServerError;
  auto status = sendProtocolError(Envoy::StreamInfo::ResponseCodeDetails::get().Overload);
  dispatching_ = latched_dispatching;
  return status;
}

Status ServerConnectionImpl::sendProtocolError(absl::string_view details) {
  // We do this here because we may get a protocol error before we have a logical stream.
  if (active_request_ == nullptr) {
    RETURN_IF_ERROR(onMessageBeginImpl());
  }
  ASSERT(active_request_);

  active_request_->response_encoder_.setDetails(details);
  if (!active_request_->response_encoder_.startedResponse()) {
    active_request_->request_decoder_->sendLocalReply(
        error_code_, CodeUtility::toString(error_code_), nullptr, absl::nullopt, details);
  }
  return okStatus();
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

void ServerConnectionImpl::releaseOutboundResponse(
    const Buffer::OwnedBufferFragmentImpl* fragment) {
  ASSERT(outbound_responses_ >= 1);
  --outbound_responses_;
  delete fragment;
}

Status ServerConnectionImpl::checkHeaderNameForUnderscores() {
#ifndef ENVOY_ENABLE_UHV
  // This check has been moved to UHV
  if (headers_with_underscores_action_ != envoy::config::core::v3::HttpProtocolOptions::ALLOW &&
      Http::HeaderUtility::headerNameContainsUnderscore(current_header_field_.getStringView())) {
    if (headers_with_underscores_action_ ==
        envoy::config::core::v3::HttpProtocolOptions::DROP_HEADER) {
      ENVOY_CONN_LOG(debug, "Dropping header with invalid characters in its name: {}", connection_,
                     current_header_field_.getStringView());
      stats_.incDroppedHeadersWithUnderscores();
      current_header_field_.clear();
      current_header_value_.clear();
    } else {
      ENVOY_CONN_LOG(debug, "Rejecting request due to header name with underscores: {}",
                     connection_, current_header_field_.getStringView());
      error_code_ = Http::Code::BadRequest;
      RETURN_IF_ERROR(sendProtocolError(Http1ResponseCodeDetails::get().InvalidUnderscore));
      stats_.incRequestsRejectedWithUnderscoresInHeaders();
      return codecProtocolError("http/1.1 protocol error: header name contains underscores");
    }
  }
#else
  // Workaround for gcc not understanding [[maybe_unused]] for class members.
  (void)headers_with_underscores_action_;
#endif
  return okStatus();
}

void ServerConnectionImpl::ActiveRequest::dumpState(std::ostream& os, int indent_level) const {
  (void)indent_level;
  os << DUMP_MEMBER_AS(
      request_url_, !request_url_.getStringView().empty() ? request_url_.getStringView() : "null");
  os << DUMP_MEMBER(response_encoder_.local_end_stream_);
}

ClientConnectionImpl::ClientConnectionImpl(Network::Connection& connection, CodecStats& stats,
                                           ConnectionCallbacks&, const Http1Settings& settings,
                                           const uint32_t max_response_headers_count,
                                           bool passing_through_proxy)
    : ConnectionImpl(connection, stats, settings, MessageType::Response, MAX_RESPONSE_HEADERS_KB,
                     max_response_headers_count),
      owned_output_buffer_(connection.dispatcher().getWatermarkFactory().createBuffer(
          [&]() -> void { this->onBelowLowWatermark(); },
          [&]() -> void { this->onAboveHighWatermark(); },
          []() -> void { /* TODO(adisuissa): handle overflow watermark */ })),
      passing_through_proxy_(passing_through_proxy) {
  owned_output_buffer_->setWatermarks(connection.bufferLimit());
  // Inform parent
  output_buffer_ = owned_output_buffer_.get();
}

bool ClientConnectionImpl::cannotHaveBody() {
  if (pending_response_.has_value() && pending_response_.value().encoder_.headRequest()) {
    ASSERT(!pending_response_done_);
    return true;
  } else if (parser_->statusCode() == Http::Code::NoContent ||
             parser_->statusCode() == Http::Code::NotModified ||
             (parser_->statusCode() >= Http::Code::OK &&
              (parser_->contentLength().has_value() && parser_->contentLength().value() == 0) &&
              !parser_->isChunked())) {
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
  pending_response_.emplace(*this, std::move(bytes_meter_before_stream_), &response_decoder);
  pending_response_done_ = false;
  return pending_response_.value().encoder_;
}

Status ClientConnectionImpl::onStatusBase(const char* data, size_t length) {
  auto& headers = absl::get<ResponseHeaderMapPtr>(headers_or_trailers_);
  StatefulHeaderKeyFormatterOptRef formatter(headers->formatter());
  if (formatter.has_value()) {
    formatter->setReasonPhrase(absl::string_view(data, length));
  }

  return okStatus();
}

Envoy::StatusOr<CallbackResult> ClientConnectionImpl::onHeadersCompleteBase() {
  ENVOY_CONN_LOG(trace, "status_code {}", connection_, enumToInt(parser_->statusCode()));

  // Handle the case where the client is closing a kept alive connection (by sending a 408
  // with a 'Connection: close' header). In this case we just let response flush out followed
  // by the remote close.
  if (!pending_response_.has_value() && !resetStreamCalled()) {
    return prematureResponseError("", parser_->statusCode());
  } else if (pending_response_.has_value()) {
    ASSERT(!pending_response_done_);
    auto& headers = absl::get<ResponseHeaderMapPtr>(headers_or_trailers_);
    ENVOY_CONN_LOG(trace, "Client: onHeadersComplete size={}", connection_, headers->size());
    headers->setStatus(enumToInt(parser_->statusCode()));

    if (parser_->statusCode() >= Http::Code::OK &&
        parser_->statusCode() < Http::Code::MultipleChoices &&
        pending_response_.value().encoder_.connectRequest()) {
      ENVOY_CONN_LOG(trace, "codec entering upgrade mode for CONNECT response.", connection_);
      handling_upgrade_ = true;
    }

    if (parser_->statusCode() < Http::Code::OK || parser_->statusCode() == Http::Code::NoContent) {
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

    if (HeaderUtility::isSpecial1xx(*headers)) {
      pending_response_.value().decoder_->decode1xxHeaders(std::move(headers));
    } else if (cannotHaveBody() && !handling_upgrade_) {
      deferred_end_stream_headers_ = true;
    } else {
      pending_response_.value().decoder_->decodeHeaders(std::move(headers), false);
    }

    // http-parser treats 1xx headers as their own complete response. Swallow the spurious
    // onMessageComplete and continue processing for purely informational headers.
    // 101-SwitchingProtocols is exempt as all data after the header is proxied through after
    // upgrading.
    if (CodeUtility::is1xx(enumToInt(parser_->statusCode())) &&
        parser_->statusCode() != Http::Code::SwitchingProtocols) {
      ignore_message_complete_for_1xx_ = true;
      // Reset to ensure no information from the 1xx headers is used for the response headers.
      headers_or_trailers_.emplace<ResponseHeaderMapPtr>(nullptr);
    }
  }

  // Here we deal with cases where the response cannot have a body by returning
  // CallbackResult::NoBody, but http_parser does not deal with it for us.
  return cannotHaveBody() ? CallbackResult::NoBody : CallbackResult::Success;
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

CallbackResult ClientConnectionImpl::onMessageCompleteBase() {
  ENVOY_CONN_LOG(trace, "message complete", connection_);
  if (ignore_message_complete_for_1xx_) {
    ignore_message_complete_for_1xx_ = false;
    return CallbackResult::Success;
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
  return parser_->pause();
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
