#include "source/extensions/filters/http/json_rpc/json_parser.h"

#include <cctype>
#include <string>

#include "envoy/buffer/buffer.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JsonRpc {

JsonParser::JsonParser(JsonDecoder& decoder) : decoder_(decoder) {}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

absl::Status JsonParser::parse(const Buffer::Instance& data) {
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

absl::Status JsonParser::finishParse() {
  if (state_ == State::Done) {
    return absl::OkStatus();
  }
  if (state_ == State::Error) {
    return absl::InvalidArgumentError("JSON parse already failed");
  }
  if (state_ == State::Init) {
    return absl::InvalidArgumentError("Empty body: no JSON object received");
  }
  // A numeric/bool/null value at the very end of the body has no trailing
  // delimiter — flush it here if we are still accumulating one.
  if (state_ == State::InPrimitiveValue) {
    if (auto s = fireValueCallback(); !s.ok()) {
      return s;
    }
  }
  return absl::InvalidArgumentError("Incomplete JSON: top-level '}' not seen");
}

// -----------------------------------------------------------------------------
// Central dispatch
// -----------------------------------------------------------------------------

absl::Status JsonParser::processByte(char c) {
  switch (state_) {
  case State::Init:             return handleInit(c);
  case State::InObject:         return handleInObject(c);
  case State::InKey:            return handleInKey(c);
  case State::AfterKey:         return handleAfterKey(c);
  case State::DispatchValue:    return handleDispatchValue(c);
  case State::InStringValue:    return handleInStringValue(c);
  case State::InPrimitiveValue: return handleInPrimitiveValue(c);
  case State::InNestedValue:    return handleInNestedValue(c);
  case State::AfterValue:       return handleAfterValue(c);
  case State::Done:
  case State::Error:
    return absl::OkStatus();
  }
  return absl::OkStatus(); // unreachable
}

// -----------------------------------------------------------------------------
// Per-state handlers
// -----------------------------------------------------------------------------

absl::Status JsonParser::handleInit(char c) {
  if (std::isspace(static_cast<unsigned char>(c))) {
    return absl::OkStatus();
  }
  if (c == '{') {
    state_ = State::InObject;
    decoder_.onObjectBegin();
    return absl::OkStatus();
  }
  return error("Expected '{' to open JSON object");
}

absl::Status JsonParser::handleInObject(char c) {
  if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
    return absl::OkStatus();
  }
  if (c == '}') {
    state_ = State::Done;
    decoder_.onObjectEnd();
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

absl::Status JsonParser::handleInKey(char c) {
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
    // Key complete: fire callback and wait for ':'.
    decoder_.onKey(accumulator_);
    accumulator_.clear();
    state_ = State::AfterKey;
    return absl::OkStatus();
  }
  accumulator_ += c;
  return absl::OkStatus();
}

absl::Status JsonParser::handleAfterKey(char c) {
  if (std::isspace(static_cast<unsigned char>(c))) {
    return absl::OkStatus();
  }
  if (c == ':') {
    state_ = State::DispatchValue;
    return absl::OkStatus();
  }
  return error(absl::StrCat("Expected ':' after key, got '", std::string(1, c), "'"));
}

absl::Status JsonParser::handleDispatchValue(char c) {
  if (std::isspace(static_cast<unsigned char>(c))) {
    return absl::OkStatus();
  }

  accumulator_.clear();
  in_escape_ = false;

  if (c == '"') {
    value_type_ = ValueType::String;
    state_ = State::InStringValue;
    return absl::OkStatus();
  }

  if (c == '{' || c == '[') {
    value_type_ = ValueType::Nested;
    accumulator_ += c;
    nested_depth_ = 1;
    in_nested_string_ = false;
    state_ = State::InNestedValue;
    return absl::OkStatus();
  }

  // Number, bool, or null.
  value_type_ = ValueType::Primitive;
  accumulator_ += c;
  state_ = State::InPrimitiveValue;
  return absl::OkStatus();
}

absl::Status JsonParser::handleInStringValue(char c) {
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
    if (auto s = fireValueCallback(); !s.ok()) {
      return s;
    }
    state_ = State::AfterValue;
    return absl::OkStatus();
  }
  accumulator_ += c;
  return absl::OkStatus();
}

absl::Status JsonParser::handleInPrimitiveValue(char c) {
  if (c == ',' || c == '}' || std::isspace(static_cast<unsigned char>(c))) {
    if (auto s = fireValueCallback(); !s.ok()) {
      return s;
    }
    if (c == ',') {
      state_ = State::InObject;
    } else if (c == '}') {
      state_ = State::Done;
      decoder_.onObjectEnd();
    } else {
      state_ = State::AfterValue;
    }
    return absl::OkStatus();
  }
  accumulator_ += c;
  return absl::OkStatus();
}

absl::Status JsonParser::handleInNestedValue(char c) {
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
      if (auto s = fireValueCallback(); !s.ok()) {
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

absl::Status JsonParser::handleAfterValue(char c) {
  if (std::isspace(static_cast<unsigned char>(c))) {
    return absl::OkStatus();
  }
  if (c == ',') {
    state_ = State::InObject;
    return absl::OkStatus();
  }
  if (c == '}') {
    state_ = State::Done;
    decoder_.onObjectEnd();
    return absl::OkStatus();
  }
  return error(absl::StrCat("Expected ',' or '}' after value, got '", std::string(1, c), "'"));
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

absl::Status JsonParser::fireValueCallback() {
  switch (value_type_) {
  case ValueType::String:
    decoder_.onStringValue(accumulator_);
    break;
  case ValueType::Primitive:
    decoder_.onPrimitiveValue(accumulator_);
    break;
  case ValueType::Nested:
    decoder_.onNestedValue(accumulator_);
    break;
  }
  accumulator_.clear();
  return absl::OkStatus();
}

absl::Status JsonParser::error(absl::string_view message) {
  state_ = State::Error;
  decoder_.onError(message);
  return absl::InvalidArgumentError(message);
}

} // namespace JsonRpc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
