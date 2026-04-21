#include "source/extensions/filters/http/json_rpc/json_rpc_parser.h"

#include <cctype>
#include <string>

#include "envoy/buffer/buffer.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JsonRpc {

JsonRpcParser::JsonRpcParser(JsonRpcDecoder& decoder) : decoder_(decoder) {}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

absl::Status JsonRpcParser::parse(const Buffer::Instance& data) {
  // Walk slices in place — no copy needed.
  for (const Buffer::RawSlice& slice : data.getRawSlices()) {
    const char* p = static_cast<const char*>(slice.mem_);
    for (size_t i = 0; i < slice.len_; ++i) {
      if (auto s = processByte(p[i]); !s.ok()) {
        return s;
      }
      if (state_ == State::Done || state_ == State::Error) {
        return absl::OkStatus();
      }
    }
  }
  return absl::OkStatus();
}

absl::Status JsonRpcParser::finishParse() {
  if (state_ == State::Done) {
    return absl::OkStatus();
  }
  if (state_ == State::Error) {
    return absl::InvalidArgumentError("JSON-RPC parse already failed");
  }
  if (state_ == State::Init) {
    return error("Empty body: no JSON-RPC object received");
  }
  // A numeric/bool/null id at the very end of the body has no trailing
  // delimiter, so flush it here if we're still accumulating one.
  if (state_ == State::InPrimitiveValue) {
    if (auto s = fireFieldCallback(); !s.ok()) {
      return s;
    }
  }
  return error("Incomplete JSON-RPC message: top-level '}' not seen");
}

// -----------------------------------------------------------------------------
// Central dispatch
// -----------------------------------------------------------------------------

absl::Status JsonRpcParser::processByte(char c) {
  switch (state_) {
  case State::Init:              return handleInit(c);
  case State::InObject:          return handleInObject(c);
  case State::InKey:             return handleInKey(c);
  case State::AfterKey:          return handleAfterKey(c);
  case State::DispatchValue:     return handleDispatchValue(c);
  case State::InStringValue:     return handleInStringValue(c);
  case State::InPrimitiveValue:  return handleInPrimitiveValue(c);
  case State::InNestedValue:     return handleInNestedValue(c);
  case State::SkipStringValue:   return handleSkipStringValue(c);
  case State::SkipPrimitiveValue: return handleSkipPrimitiveValue(c);
  case State::SkipNestedValue:   return handleSkipNestedValue(c);
  case State::AfterValue:        return handleAfterValue(c);
  case State::Done:
  case State::Error:
    return absl::OkStatus();
  }
  return absl::OkStatus(); // unreachable
}

// -----------------------------------------------------------------------------
// Per-state handlers
// -----------------------------------------------------------------------------

absl::Status JsonRpcParser::handleInit(char c) {
  if (std::isspace(static_cast<unsigned char>(c))) {
    return absl::OkStatus();
  }
  if (c == '{') {
    state_ = State::InObject;
    decoder_.onJsonRpcBegin();
    return absl::OkStatus();
  }
  return error("Expected '{' to open JSON-RPC message");
}

absl::Status JsonRpcParser::handleInObject(char c) {
  if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
    // ',' is also accepted here for leniency (should arrive via AfterValue,
    // but some parsers emit it in a way that lands here after the first key).
    return absl::OkStatus();
  }
  if (c == '}') {
    // Top-level object closed.
    state_ = State::Done;
    decoder_.onJsonRpcComplete();
    return absl::OkStatus();
  }
  if (c == '"') {
    accumulator_.clear();
    in_escape_ = false;
    state_ = State::InKey;
    return absl::OkStatus();
  }
  return error(absl::StrCat("Unexpected '", std::string(1, c), "' in JSON object"));
}

// Reading key characters between the enclosing '"' marks.
absl::Status JsonRpcParser::handleInKey(char c) {
  if (in_escape_) {
    accumulator_ += c;
    in_escape_ = false;
    return absl::OkStatus();
  }
  if (c == '\\') {
    in_escape_ = true;
    return absl::OkStatus();
  }
  if (c == '"') {
    // Key string complete: classify it and move on.
    current_field_ = classifyKey(accumulator_);
    accumulator_.clear();
    state_ = State::AfterKey;
    return absl::OkStatus();
  }
  accumulator_ += c;
  return absl::OkStatus();
}

absl::Status JsonRpcParser::handleAfterKey(char c) {
  if (std::isspace(static_cast<unsigned char>(c))) {
    return absl::OkStatus();
  }
  if (c == ':') {
    state_ = State::DispatchValue;
    return absl::OkStatus();
  }
  return error(absl::StrCat("Expected ':' after key, got '", std::string(1, c), "'"));
}

// Peek at first non-whitespace byte of the value to decide its type.
absl::Status JsonRpcParser::handleDispatchValue(char c) {
  if (std::isspace(static_cast<unsigned char>(c))) {
    return absl::OkStatus();
  }

  accumulator_.clear();
  in_escape_ = false;

  if (c == '"') {
    // String value — interesting for: method, jsonrpc, and string id.
    switch (current_field_) {
    case CurrentField::Method:
    case CurrentField::Jsonrpc:
    case CurrentField::Id:
      state_ = State::InStringValue;
      break;
    default:
      state_ = State::SkipStringValue;
      break;
    }
    return absl::OkStatus();
  }

  if (c == '{' || c == '[') {
    // Nested value — interesting for: params, and object/array id.
    accumulator_ += c;
    nested_depth_ = 1;
    in_nested_string_ = false;
    switch (current_field_) {
    case CurrentField::Params:
    case CurrentField::Id:
      state_ = State::InNestedValue;
      break;
    default:
      state_ = State::SkipNestedValue;
      break;
    }
    return absl::OkStatus();
  }

  // Primitive value (number, true, false, null).
  // Only id can be a non-string primitive at the top level of JSON-RPC.
  accumulator_ += c;
  switch (current_field_) {
  case CurrentField::Id:
    state_ = State::InPrimitiveValue;
    break;
  default:
    state_ = State::SkipPrimitiveValue;
    break;
  }
  return absl::OkStatus();
}

// Reading a string value for a known key.
absl::Status JsonRpcParser::handleInStringValue(char c) {
  if (in_escape_) {
    accumulator_ += c;
    in_escape_ = false;
    return absl::OkStatus();
  }
  if (c == '\\') {
    in_escape_ = true;
    return absl::OkStatus();
  }
  if (c == '"') {
    // Value complete — fire callback then wait for ',' or '}'.
    if (auto s = fireFieldCallback(); !s.ok()) {
      return s;
    }
    state_ = State::AfterValue;
    return absl::OkStatus();
  }
  accumulator_ += c;
  return absl::OkStatus();
}

// Reading a primitive (number / bool / null) value for a known key.
// Primitives are terminated by ',' '}' or whitespace; we do NOT consume the
// terminator here — re-dispatch it via handleAfterValue or handleInObject.
absl::Status JsonRpcParser::handleInPrimitiveValue(char c) {
  if (c == ',' || c == '}' || std::isspace(static_cast<unsigned char>(c))) {
    if (auto s = fireFieldCallback(); !s.ok()) {
      return s;
    }
    // Re-dispatch the terminator.
    if (c == ',') {
      state_ = State::InObject; // expect next key
    } else if (c == '}') {
      state_ = State::Done;
      decoder_.onJsonRpcComplete();
    } else {
      state_ = State::AfterValue;
    }
    return absl::OkStatus();
  }
  accumulator_ += c;
  return absl::OkStatus();
}

// Reading a nested (object or array) value for a known key.
//
// The tricky part: '{' / '[' / '}' / ']' inside JSON strings within the nested
// value must not affect the depth counter. We track in_nested_string_ for this.
absl::Status JsonRpcParser::handleInNestedValue(char c) {
  accumulator_ += c;

  if (in_escape_) {
    in_escape_ = false;
    return absl::OkStatus();
  }

  if (in_nested_string_) {
    if (c == '\\') {
      in_escape_ = true;
    } else if (c == '"') {
      in_nested_string_ = false;
    }
    return absl::OkStatus();
  }

  // We are not inside a nested string.
  switch (c) {
  case '"':
    in_nested_string_ = true;
    break;
  case '{':
  case '[':
    ++nested_depth_;
    break;
  case '}':
  case ']':
    --nested_depth_;
    if (nested_depth_ == 0) {
      // The closing bracket of this value was just consumed.
      if (auto s = fireFieldCallback(); !s.ok()) {
        return s;
      }
      state_ = State::AfterValue;
    }
    break;
  default:
    break;
  }
  return absl::OkStatus();
}

// Skipping a string value for an unknown key.
absl::Status JsonRpcParser::handleSkipStringValue(char c) {
  if (in_escape_) {
    in_escape_ = false;
    return absl::OkStatus();
  }
  if (c == '\\') {
    in_escape_ = true;
    return absl::OkStatus();
  }
  if (c == '"') {
    state_ = State::AfterValue;
  }
  return absl::OkStatus();
}

// Skipping a primitive value for an unknown key.
absl::Status JsonRpcParser::handleSkipPrimitiveValue(char c) {
  if (c == ',' || c == '}' || std::isspace(static_cast<unsigned char>(c))) {
    if (c == ',') {
      state_ = State::InObject;
    } else if (c == '}') {
      state_ = State::Done;
      decoder_.onJsonRpcComplete();
    } else {
      state_ = State::AfterValue;
    }
  }
  return absl::OkStatus();
}

// Skipping a nested value for an unknown key — same depth tracking as
// InNestedValue but no accumulation and no callback.
absl::Status JsonRpcParser::handleSkipNestedValue(char c) {
  if (in_escape_) {
    in_escape_ = false;
    return absl::OkStatus();
  }
  if (in_nested_string_) {
    if (c == '\\') {
      in_escape_ = true;
    } else if (c == '"') {
      in_nested_string_ = false;
    }
    return absl::OkStatus();
  }
  switch (c) {
  case '"':
    in_nested_string_ = true;
    break;
  case '{':
  case '[':
    ++nested_depth_;
    break;
  case '}':
  case ']':
    --nested_depth_;
    if (nested_depth_ == 0) {
      state_ = State::AfterValue;
    }
    break;
  default:
    break;
  }
  return absl::OkStatus();
}

// After a value: expect ',' (next pair) or '}' (object end).
absl::Status JsonRpcParser::handleAfterValue(char c) {
  if (std::isspace(static_cast<unsigned char>(c))) {
    return absl::OkStatus();
  }
  if (c == ',') {
    state_ = State::InObject;
    return absl::OkStatus();
  }
  if (c == '}') {
    state_ = State::Done;
    decoder_.onJsonRpcComplete();
    return absl::OkStatus();
  }
  return error(absl::StrCat("Expected ',' or '}' after value, got '", std::string(1, c), "'"));
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

absl::Status JsonRpcParser::fireFieldCallback() {
  switch (current_field_) {
  case CurrentField::Method:
    // accumulator_ holds the raw (unescaped) method string bytes.
    decoder_.onMethod(accumulator_);
    break;

  case CurrentField::Id: {
    // Parse the accumulated bytes as JSON to produce a typed nlohmann::json id.
    // For string ids the accumulator holds the content without surrounding quotes;
    // for primitives it holds the raw token (e.g. "42", "null").
    nlohmann::json id;
    if (state_ == State::InStringValue || state_ == State::Done) {
      // We were reading a string — wrap in quotes before parsing.
      auto parsed = nlohmann::json::parse('"' + accumulator_ + '"',
                                          /*cb=*/nullptr, /*allow_exceptions=*/false);
      if (parsed.is_discarded()) {
        return error("Failed to parse JSON-RPC id string");
      }
      id = std::move(parsed);
    } else {
      // Primitive token (number or null).
      auto parsed = nlohmann::json::parse(accumulator_,
                                          /*cb=*/nullptr, /*allow_exceptions=*/false);
      if (parsed.is_discarded()) {
        return error(absl::StrCat("Failed to parse JSON-RPC id value: ", accumulator_));
      }
      id = std::move(parsed);
    }
    decoder_.onId(id);
    break;
  }

  case CurrentField::Params: {
    // accumulator_ holds the complete nested JSON bytes (e.g. "{...}" or "[...]").
    auto parsed = nlohmann::json::parse(accumulator_,
                                        /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) {
      return error("Failed to parse JSON-RPC params value");
    }
    decoder_.onParams(parsed);
    break;
  }

  case CurrentField::Jsonrpc:
    // "jsonrpc" field value — validated implicitly by protocol callers; not
    // forwarded to the decoder since it adds no routing or dispatch information.
    break;

  case CurrentField::Unknown:
    break;
  }

  accumulator_.clear();
  current_field_ = CurrentField::Unknown;
  return absl::OkStatus();
}

absl::Status JsonRpcParser::error(absl::string_view message) {
  state_ = State::Error;
  decoder_.onError(message);
  return absl::InvalidArgumentError(message);
}

JsonRpcParser::CurrentField JsonRpcParser::classifyKey(absl::string_view key) const {
  if (key == "method") return CurrentField::Method;
  if (key == "id")     return CurrentField::Id;
  if (key == "params") return CurrentField::Params;
  if (key == "jsonrpc") return CurrentField::Jsonrpc;
  return CurrentField::Unknown;
}

} // namespace JsonRpc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
