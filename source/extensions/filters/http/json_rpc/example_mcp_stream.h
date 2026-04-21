#pragma once

// Example: wiring JsonRpcConnectionManager for MCP routing.
//
// Shows how a caller implements the two interfaces and what the callback
// sequence looks like for a typical MCP tools/call request.
//
// Analogous to how HttpConnectionManagerImpl implements
// ServerConnectionCallbacks::newStream() and RequestDecoder.

#include <string>

#include "source/extensions/filters/http/json_rpc/json_rpc_connection_manager.h"
#include "source/extensions/filters/http/json_rpc/json_rpc_decoder.h"

#include "absl/strings/string_view.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JsonRpc {

// ---------------------------------------------------------------------------
// Per-request decoder
//
// One instance per HTTP request carrying a JSON-RPC body.
// Implements JsonRpcDecoder; receives streaming field events from JsonRpcParser.
//
// For the MCP use-case the most valuable early callback is onMethod():
// it fires as soon as the "method" string is consumed, before the (potentially
// large) "params" object has arrived — enabling routing decisions without
// buffering the full body.
// ---------------------------------------------------------------------------
class McpStreamDecoder : public JsonRpcDecoder {
public:
  // Called when opening '{' is consumed.  No fields are available yet.
  // → Analogous to Http::RequestDecoder::decodeHeaders(headers, end_stream=false)
  void onJsonRpcBegin() override {
    // Nothing useful yet — just record that parsing started.
  }

  // Called as soon as the "method" string value is complete.
  // Fires BEFORE onParams, potentially on the very first body chunk if
  // "method" appears early in the JSON object (the common case).
  //
  // → Analogous to decodeHeaders() firing before decodeData():
  //   routing decisions can be made here without waiting for the body.
  void onMethod(absl::string_view method) override {
    method_ = std::string(method);
    // E.g.: if method_ == "tools/call" → pre-authorise the upstream route.
    //        if method_ == "initialize"  → reply directly without forwarding.
  }

  // Called as soon as the "id" field is complete.
  // May fire before or after onMethod depending on document order.
  void onId(const nlohmann::json& id) override {
    id_ = id;
    // Save for building the JSON-RPC response envelope later.
  }

  // Called once the entire "params" value has been consumed.
  // For large payloads this fires only after all nested bytes arrive, but
  // onMethod and onId will already have fired earlier.
  void onParams(const nlohmann::json& params) override {
    params_ = params;
    // E.g.: validate params schema, extract tool name for authz, etc.
    if (method_ == "tools/call") {
      tool_name_ = params.value("name", "");
    }
  }

  // Called when the top-level '}' is consumed: full message is available.
  // → Analogous to decodeData(data, end_stream=true).
  void onJsonRpcComplete() override {
    // At this point method_, id_, params_ are all populated (if present).
    // Forward to upstream, apply policy, generate local response, etc.
  }

  void onError(absl::string_view message) override {
    parse_error_ = std::string(message);
    // Send a JSON-RPC parse error response (-32700).
  }

  // Accessors for the owning filter to read parsed fields.
  const std::string& method() const { return method_; }
  const nlohmann::json& id() const { return id_; }
  const nlohmann::json& params() const { return params_; }
  const std::string& toolName() const { return tool_name_; }
  bool hasError() const { return !parse_error_.empty(); }

private:
  std::string method_;
  nlohmann::json id_;
  nlohmann::json params_;
  std::string tool_name_;
  std::string parse_error_;
};

// ---------------------------------------------------------------------------
// Connection-level callbacks
//
// One instance shared across all requests on a connection (or per-filter-chain).
// Implements JsonRpcConnectionManagerCallbacks::newStream().
//
// → Analogous to ConnectionManagerImpl implementing
//   ServerConnectionCallbacks::newStream().
// ---------------------------------------------------------------------------
class McpConnectionCallbacks : public JsonRpcConnectionManagerCallbacks {
public:
  // Called once per JSON-RPC HTTP request when the filter sees the right
  // Content-Type in decodeHeaders().  Returns a per-request decoder.
  JsonRpcDecoder& newStream(const Http::RequestHeaderMap& /*request_headers*/,
                             Http::StreamDecoderFilterCallbacks& /*decoder_callbacks*/) override {
    // Create a new per-request decoder.
    // In a real filter the decoder would be stored on the ActiveStream equivalent
    // and freed when the stream completes.
    active_stream_ = std::make_unique<McpStreamDecoder>();
    return *active_stream_;
  }

  McpStreamDecoder* activeStream() { return active_stream_.get(); }

private:
  std::unique_ptr<McpStreamDecoder> active_stream_;
};

// ---------------------------------------------------------------------------
// Callback sequence for:
//   POST /mcp HTTP/1.1
//   Content-Type: application/json
//
//   {"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"search","arguments":{"q":"envoy"}}}
//
// decodeHeaders()      → isJsonRpcContentType=true → newStream() → parser created
// decodeData(chunk1)   → parse("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\"")
//                          → onJsonRpcBegin()        (after '{')
//                          → onId(1)                  (after id value)
//                          → onMethod("tools/call")   (after method string) ← early!
// decodeData(chunk2, end_stream=true)
//                      → parse(",\"params\":{\"name\":\"search\",\"arguments\":{\"q\":\"envoy\"}}}")
//                          → onParams({name:search,…}) (after closing '}' of params)
//                          → onJsonRpcComplete()       (after top-level '}')
//                       → finishParse() → ok
// ---------------------------------------------------------------------------

} // namespace JsonRpc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
