#pragma once

#include <string>

#include "envoy/buffer/buffer.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/json_rpc/json_rpc_decoder.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JsonRpc {

/**
 * Streaming JSON-RPC parser, analogous to Http::Http1::ConnectionImpl.
 *
 * The HTTP/1.1 codec wraps llhttp: it feeds raw TCP bytes into a state machine
 * that fires onHeadersComplete / onBody / onMessageComplete callbacks, which the
 * codec translates into RequestDecoder::decodeHeaders / decodeData calls.
 *
 * JsonRpcParser does the same one layer up: it accepts Buffer::Instance chunks
 * (already HTTP-decoded body data) and feeds them through a JSON state machine
 * that fires JsonRpcDecoder callbacks as each top-level field is recognised.
 *
 * Design properties that mirror the HTTP codec:
 *
 *  - Incremental / zero-copy: parse() walks raw buffer slices in place; no full
 *    body copy is required before callbacks fire.
 *  - Early callback for scalars: onMethod and onId (when id is a scalar) fire as
 *    soon as the closing token is consumed, before "params" has arrived.
 *    This mirrors how decodeHeaders() fires before decodeData().
 *  - Depth-tracked accumulation for nested values: "params" (and object/array ids)
 *    are accumulated until the matching closing bracket, then delivered as a
 *    parsed nlohmann::json value.  Strings inside nested values are tracked so
 *    that '{' / '[' inside JSON strings are never mistaken for nesting.
 */
class JsonRpcParser : public Logger::Loggable<Logger::Id::filter> {
public:
  explicit JsonRpcParser(JsonRpcDecoder& decoder);

  /**
   * Feed one buffer chunk into the parser.
   *
   * Analogous to ServerConnection::dispatch(Buffer::Instance&): may fire
   * zero or more decoder callbacks synchronously before returning.
   *
   * @return OkStatus on success, or an error status if the JSON is malformed.
   *         On error, onError() has already been called on the decoder.
   */
  absl::Status parse(const Buffer::Instance& data);

  /**
   * Signal end of input and validate completeness.
   *
   * Should be called when the HTTP body's end_stream=true is seen.
   * Returns an error if the top-level object was never closed.
   */
  absl::Status finishParse();

private:
  // -----------------------------------------------------------------------
  // State machine
  // -----------------------------------------------------------------------
  //
  // States proceed in roughly this order for a well-formed message:
  //
  //   Init → InObject ──► InKey → AfterKey → DispatchValue
  //                  ↑                              │
  //                  │         ┌────────────────────┤
  //                  │         ▼                    ▼
  //                  │   InStringValue        InPrimitiveValue
  //                  │   InNestedValue        SkipStringValue
  //                  │   SkipNestedValue      SkipPrimitiveValue
  //                  │         │                    │
  //                  └─── AfterValue ───────────────┘
  //                       (or Done when '}' seen)
  //
  enum class State {
    // Haven't seen the opening '{' yet.
    Init,
    // Inside top-level object; expect '"' (next key), ',' (between pairs), or '}'.
    InObject,
    // Reading key bytes between the surrounding '"' characters.
    InKey,
    // Key closed; waiting for ':'.
    AfterKey,
    // ':' seen; peeking at the next non-whitespace byte to dispatch value type.
    DispatchValue,
    // Reading a string value for a known key (method, jsonrpc, or string id).
    InStringValue,
    // Reading a number/bool/null value for a known key (numeric or null id).
    InPrimitiveValue,
    // Reading a {…} or […] value for a known key (params, or object/array id);
    // nested_depth_ tracks how deep we are so we know when it ends.
    InNestedValue,
    // Skipping a string value for an unknown key.
    SkipStringValue,
    // Skipping a primitive value for an unknown key.
    SkipPrimitiveValue,
    // Skipping a nested value for an unknown key.
    SkipNestedValue,
    // Value ended; expect ',' or '}'.
    AfterValue,
    // Top-level '}' consumed; parsing complete.
    Done,
    // Unrecoverable parse error.
    Error,
  };

  // Which top-level field the parser is currently reading.
  enum class CurrentField {
    Unknown,
    Jsonrpc, // "jsonrpc" — validated but not forwarded to decoder
    Method,  // "method"  — string; fires onMethod
    Id,      // "id"      — string, number, or null; fires onId
    Params,  // "params"  — object or array; fires onParams
  };

  absl::Status processByte(char c);

  // Per-state handlers, each called with the current byte.
  absl::Status handleInit(char c);
  absl::Status handleInObject(char c);
  absl::Status handleInKey(char c);
  absl::Status handleAfterKey(char c);
  absl::Status handleDispatchValue(char c);
  absl::Status handleInStringValue(char c);
  absl::Status handleInPrimitiveValue(char c);
  absl::Status handleInNestedValue(char c);
  absl::Status handleSkipStringValue(char c);
  absl::Status handleSkipPrimitiveValue(char c);
  absl::Status handleSkipNestedValue(char c);
  absl::Status handleAfterValue(char c);

  // Fire the appropriate decoder callback for the fully-parsed current field.
  absl::Status fireFieldCallback();

  // Transition to Error state, call decoder_.onError(), return error status.
  absl::Status error(absl::string_view message);

  // Map a key string to a known CurrentField enum value.
  CurrentField classifyKey(absl::string_view key) const;

  // -----------------------------------------------------------------------
  // Fields
  // -----------------------------------------------------------------------

  JsonRpcDecoder& decoder_;
  State state_{State::Init};
  CurrentField current_field_{CurrentField::Unknown};

  // Accumulates characters for the key being read, a string value, a
  // primitive value, or an entire nested value (params / object id).
  std::string accumulator_;

  // Depth counter for InNestedValue / SkipNestedValue.
  // Starts at 1 when the opening '{' or '[' is seen, decremented on '}'/'}'.
  // Value complete when it returns to 0.
  int nested_depth_{0};

  // True while the next character inside any string is an escape sequence.
  bool in_escape_{false};

  // True while iterating through a string token inside a nested value.
  // Required to avoid treating '{' / '[' / '}' / ']' inside JSON strings as
  // structural tokens when counting nesting depth.
  bool in_nested_string_{false};
};

} // namespace JsonRpc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
