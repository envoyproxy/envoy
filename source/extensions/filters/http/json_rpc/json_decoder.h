#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JsonRpc {

/**
 * SAX-style callback interface for streaming JSON parsing.
 *
 * JsonParser fires these callbacks as it processes a top-level JSON object
 * from an HTTP body — one layer below JSON-RPC.  This interface has no
 * awareness of JSON-RPC field names or semantics.
 *
 * Callback ordering for a well-formed top-level object:
 *
 *   onObjectBegin
 *     → (onKey → onStringValue | onPrimitiveValue | onNestedValue)*
 *   onObjectEnd
 *
 * onError terminates the sequence; no further callbacks fire after it.
 *
 * Contrast with JsonRpcDecoder, which sits one layer higher and maps these
 * structural events onto JSON-RPC field semantics (onMethod, onId, onParams).
 */
class JsonDecoder {
public:
  virtual ~JsonDecoder() = default;

  /** Called when the opening '{' of the top-level object is consumed. */
  virtual void onObjectBegin() = 0;

  /** Called when the closing '}' of the top-level object is consumed. */
  virtual void onObjectEnd() = 0;

  /**
   * Called when a top-level key string is fully parsed.
   *
   * Fires between onObjectBegin and onObjectEnd, once per key-value pair,
   * before the corresponding value callback.  Escape sequences inside the
   * key are passed through verbatim (not decoded).
   */
  virtual void onKey(absl::string_view key) = 0;

  /**
   * Called when a string value at the top level is fully parsed.
   *
   * @param value  The string content without surrounding quotes.
   *               Escape sequences are passed through verbatim.
   */
  virtual void onStringValue(absl::string_view value) = 0;

  /**
   * Called when a non-string primitive at the top level is fully parsed.
   *
   * Covers numbers, true, false, and null.
   * @param raw  The raw token bytes (e.g. "42", "true", "null").
   */
  virtual void onPrimitiveValue(absl::string_view raw) = 0;

  /**
   * Called when a nested object or array value at the top level is fully
   * parsed (depth counter returns to zero after the opening bracket).
   *
   * @param raw_json  The complete raw bytes of the nested value, including
   *                  the surrounding '{'/'}' or '['/']'.
   */
  virtual void onNestedValue(absl::string_view raw_json) = 0;

  /** Called on any parse error.  No further callbacks fire after this. */
  virtual void onError(absl::string_view message) = 0;
};

} // namespace JsonRpc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
