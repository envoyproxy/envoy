#include "source/extensions/filters/http/json_rpc/json_rpc_connection_manager.h"

#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"

#include "source/common/http/headers.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JsonRpc {

// The Content-Type values that indicate a JSON-RPC body.
// JSON-RPC 2.0 spec requires "application/json"; "application/json-rpc" is
// a historical variant.
static constexpr absl::string_view kContentTypeJson = "application/json";
static constexpr absl::string_view kContentTypeJsonRpc = "application/json-rpc";

JsonRpcConnectionManager::JsonRpcConnectionManager(
    JsonRpcConnectionManagerCallbacks& callbacks)
    : callbacks_(callbacks) {}

// -----------------------------------------------------------------------------
// Http::StreamDecoderFilter
// -----------------------------------------------------------------------------

Http::FilterHeadersStatus
JsonRpcConnectionManager::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
  // Only intercept requests that carry a JSON-RPC body.
  // Pass-through everything else untouched — just like how HCM's codec ignores
  // traffic on connections it didn't create.
  if (!isJsonRpcContentType(headers)) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Obtain the per-request decoder from the application layer.
  // Mirrors ServerConnectionImpl::onMessageBeginBase() calling callbacks_.newStream().
  decoder_ = &callbacks_.newStream(headers, *decoder_callbacks_);

  // Create the per-request streaming parser bound to this decoder.
  parser_ = std::make_unique<JsonRpcParser>(*decoder_);

  if (end_stream) {
    // No body will follow — finish immediately (e.g. GET with Content-Type set).
    if (auto s = parser_->finishParse(); !s.ok()) {
      ENVOY_LOG(debug, "json_rpc: finishParse on empty body: {}", s.message());
    }
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus
JsonRpcConnectionManager::decodeData(Buffer::Instance& data, bool end_stream) {
  // If this request isn't JSON-RPC, or decodeHeaders was never called
  // (e.g. filter inserted mid-chain after headers), do nothing.
  if (parser_ == nullptr) {
    return Http::FilterDataStatus::Continue;
  }

  // Feed the chunk into the streaming parser.
  // The parser walks the buffer's raw slices in place — no copy.
  // Callbacks fire synchronously inside parse(): onMethod and onId can fire
  // here even when end_stream is false, before params has fully arrived.
  //
  // This is the exact analogue of how llhttp fires onHeadersComplete from
  // within ConnectionImpl::dispatch() on the first chunk that completes headers,
  // before any body chunks have been dispatched.
  if (auto s = parser_->parse(data); !s.ok()) {
    ENVOY_LOG(debug, "json_rpc: parse error: {}", s.message());
    // onError() was already called on the decoder (AiFilterChain) inside parse().
    // Send a JSON-RPC parse-error response and stop the filter chain — the
    // request must not reach the upstream with a malformed body.
    sendParseError();
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  if (end_stream) {
    // Signal end-of-body: validates the top-level object was fully closed.
    // Mirrors the codec calling decodeData(data, end_stream=true) on message complete.
    if (auto s = parser_->finishParse(); !s.ok()) {
      ENVOY_LOG(debug, "json_rpc: incomplete message at end of stream: {}", s.message());
      sendParseError();
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
  }

  return Http::FilterDataStatus::Continue;
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

void JsonRpcConnectionManager::sendParseError() {
  // JSON-RPC 2.0 §5.1: parse errors use error code -32700.
  // HTTP 400 is appropriate here because the body is not valid JSON — this is
  // a transport-level error, not an application-level JSON-RPC error.
  constexpr absl::string_view body =
      R"({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"Parse error"}})";
  decoder_callbacks_->sendLocalReply(
      Http::Code::BadRequest, body,
      [](Http::ResponseHeaderMap& headers) { headers.setContentType("application/json"); },
      absl::nullopt, "json_rpc_parse_error");
}

bool JsonRpcConnectionManager::isJsonRpcContentType(const Http::RequestHeaderMap& headers) {
  const auto content_type = headers.getContentTypeValue();
  if (content_type.empty()) {
    return false;
  }
  // Strip parameters (e.g. "; charset=utf-8") before comparing.
  const auto semicolon = content_type.find(';');
  const absl::string_view media_type = (semicolon == absl::string_view::npos)
                                           ? content_type
                                           : content_type.substr(0, semicolon);
  // Trim trailing whitespace.
  const auto last = media_type.find_last_not_of(' ');
  const absl::string_view trimmed =
      (last == absl::string_view::npos) ? media_type : media_type.substr(0, last + 1);

  return trimmed == kContentTypeJson || trimmed == kContentTypeJsonRpc;
}

} // namespace JsonRpc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
