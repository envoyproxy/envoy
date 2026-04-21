#pragma once

#include "absl/strings/string_view.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JsonRpc {

/**
 * Callback interface for JSON-RPC message events.
 *
 * Mirrors Http::RequestDecoder: just as the HTTP codec fires decodeHeaders() and
 * decodeData() as HTTP framing is parsed from raw bytes, JsonRpcParser fires these
 * callbacks as JSON-RPC fields are extracted from the HTTP body chunks.
 *
 * Callback ordering reflects document order, not a fixed schema order:
 *
 *   onJsonRpcBegin
 *     → onMethod / onId / onParams  (in the order they appear in the payload)
 *   onJsonRpcComplete
 *
 * Implementations should expect any subset and any order of the middle three.
 */
class JsonRpcDecoder {
public:
  virtual ~JsonRpcDecoder() = default;

  /**
   * Called when the opening '{' of the top-level JSON-RPC object is seen.
   * Analogous to Http::RequestDecoder::decodeHeaders() with end_stream=false:
   * the message has begun but no fields are available yet.
   */
  virtual void onJsonRpcBegin() = 0;

  /**
   * Called when the "id" field value is fully parsed.
   *
   * Per JSON-RPC 2.0 §4, id MUST be a string, number, or null. The value is
   * delivered as a parsed nlohmann::json node so callers can distinguish types
   * without re-parsing. Fires as soon as the id value's closing token is consumed,
   * which may be before "params" is seen.
   *
   * Not called for notifications (JSON-RPC messages without an "id" field).
   */
  virtual void onId(const nlohmann::json& id) = 0;

  /**
   * Called when the "method" string value is fully parsed.
   *
   * Fires as soon as the closing '"' of the method string is consumed,
   * allowing routing decisions before the (potentially large) "params" body
   * has arrived — analogous to how decodeHeaders() fires before decodeData().
   */
  virtual void onMethod(absl::string_view method) = 0;

  /**
   * Called when the "params" value is fully parsed.
   *
   * For large params objects this fires only after all nested bytes have been
   * consumed and the depth counter returns to the top-level. The value is
   * delivered as a fully parsed nlohmann::json node.
   */
  virtual void onParams(const nlohmann::json& params) = 0;

  /**
   * Called when the closing '}' of the top-level JSON-RPC object is consumed.
   * Analogous to Http::RequestDecoder::decodeData() with end_stream=true.
   *
   * After this callback the parser will not fire any further callbacks.
   */
  virtual void onJsonRpcComplete() = 0;

  /**
   * Called on any parse error (malformed JSON, unexpected token, etc.).
   * No further callbacks will fire after onError().
   */
  virtual void onError(absl::string_view message) = 0;
};

} // namespace JsonRpc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
