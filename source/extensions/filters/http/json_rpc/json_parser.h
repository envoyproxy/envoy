#pragma once

#include <string>

#include "envoy/buffer/buffer.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/json_rpc/json_decoder.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JsonRpc {

/**
 * Streaming JSON parser that translates HTTP body bytes into JsonDecoder events.
 *
 * This is the generic JSON layer in the two-stage translation pipeline:
 *
 *   HTTP body bytes  →  JsonParser  →  JsonDecoder callbacks
 *                                             ↓
 *                                    JsonRpcTranslator
 *                                             ↓
 *                                    JsonRpcDecoder callbacks
 *
 * JsonParser is analogous to Http::Http1::ConnectionImpl feeding raw TCP bytes
 * into llhttp: it accepts Buffer::Instance chunks (already HTTP-decoded body
 * data) and walks them byte-by-byte through a state machine that fires
 * JsonDecoder callbacks as each top-level field is recognised.
 *
 * Design properties:
 *  - Zero-copy incremental: parse() walks raw buffer slices in place.
 *  - Early callbacks for scalars: onKey/onStringValue/onPrimitiveValue fire as
 *    soon as the closing token is consumed, before subsequent fields arrive.
 *  - Depth-tracked accumulation for nested values: onNestedValue fires with the
 *    complete raw JSON once the matching closing bracket is consumed.
 *  - No JSON-RPC awareness: field-name semantics live in JsonRpcTranslator.
 */
class JsonParser : public Logger::Loggable<Logger::Id::filter> {
public:
  explicit JsonParser(JsonDecoder& decoder);

  /**
   * Feed one buffer chunk into the parser.
   *
   * May fire zero or more JsonDecoder callbacks synchronously before returning.
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
  //   Init → InObject (onObjectBegin)
  //   InObject → InKey | Done/onObjectEnd
  //   InKey → AfterKey (fires onKey on closing '"')
  //   AfterKey → DispatchValue (on ':')
  //   DispatchValue → InStringValue | InPrimitiveValue | InNestedValue
  //   InStringValue → AfterValue (fires onStringValue)
  //   InPrimitiveValue → AfterValue/Done (fires onPrimitiveValue)
  //   InNestedValue → AfterValue (fires onNestedValue when depth = 0)
  //   AfterValue → InObject (on ',') | Done/onObjectEnd (on '}')
  //
  enum class State {
    Init,
    InObject,
    InKey,
    AfterKey,
    DispatchValue,
    InStringValue,
    InPrimitiveValue,
    InNestedValue,
    AfterValue,
    Done,
    Error,
  };

  // What kind of value is currently being read.
  // Used by fireValueCallback() to select the right JsonDecoder method.
  enum class ValueType { String, Primitive, Nested };

  absl::Status processByte(char c);

  absl::Status handleInit(char c);
  absl::Status handleInObject(char c);
  absl::Status handleInKey(char c);
  absl::Status handleAfterKey(char c);
  absl::Status handleDispatchValue(char c);
  absl::Status handleInStringValue(char c);
  absl::Status handleInPrimitiveValue(char c);
  absl::Status handleInNestedValue(char c);
  absl::Status handleAfterValue(char c);

  // Fire the appropriate JsonDecoder callback for the completed value.
  absl::Status fireValueCallback();

  // Transition to Error state, call decoder_.onError(), return error status.
  absl::Status error(absl::string_view message);

  // -----------------------------------------------------------------------
  // Fields
  // -----------------------------------------------------------------------

  JsonDecoder& decoder_;
  State state_{State::Init};
  ValueType value_type_{ValueType::String};

  // Accumulates characters for the key being read, a string value, a
  // primitive value, or an entire nested value.
  std::string accumulator_;

  // Depth counter for InNestedValue.
  // Set to 1 on the opening '{' or '[', decremented on '}'/'}'.
  // Value complete when it returns to 0.
  int nested_depth_{0};

  // True while the next character inside any string is an escape sequence.
  bool in_escape_{false};

  // True while iterating through a string token inside a nested value.
  // Prevents '{' / '[' / '}' / ']' inside JSON strings from affecting the
  // nesting depth counter.
  bool in_nested_string_{false};
};

} // namespace JsonRpc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
