#include "source/extensions/filters/network/common/redis/codec_impl.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/platform.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"

#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

// Decoder DoS-defense limits (kMaxRespElements, kMaxBulkStringLength, kMaxDoubleTokenLength,
// kMaxBigNumberTokenLength, kMaxConsecutiveAttributes, kMaxNestingDepth, kMaxTotalElements) are
// public constants on DecoderImpl in codec_impl.h so tests can reference them by name.

namespace {

bool isValidResp3BigNumber(absl::string_view value) {
  if (value.empty()) {
    return false;
  }

  size_t digit_start = 0;
  if (value[0] == '-' || value[0] == '+') {
    if (value.size() == 1) {
      return false;
    }
    digit_start = 1;
  }

  for (size_t i = digit_start; i < value.size(); ++i) {
    if (value[i] < '0' || value[i] > '9') {
      return false;
    }
  }

  return true;
}

bool isValidResp3VerbatimString(absl::string_view value) {
  return value.size() >= 4 && value[3] == ':';
}

// Debug-print body shared by the Array/Set/Push arms of RespValue::toString(), which differ
// only in the type marker before the opening bracket.
std::string joinAggregate(const char* prefix, const std::vector<RespValue>& values) {
  std::string ret(prefix);
  ret += "[";
  for (uint64_t i = 0; i < values.size(); i++) {
    ret += values[i].toString();
    if (i != values.size() - 1) {
      ret += ", ";
    }
  }
  return ret + "]";
}

} // namespace

std::string RespValue::toString() const {
  switch (type_) {
  case RespType::Array:
    return joinAggregate("", asArray());
  case RespType::CompositeArray: {
    std::string ret = "[";
    uint64_t i = 0;
    for (const RespValue& value : asCompositeArray()) {
      ret += value.toString();
      if (++i != asCompositeArray().size()) {
        ret += ", ";
      }
    }
    return ret + "]";
  }
  case RespType::SimpleString:
  case RespType::BulkString:
  case RespType::Error:
    return fmt::format("\"{}\"", asString());
  case RespType::Null:
    return "null";
  case RespType::Integer:
    return std::to_string(asInteger());
  case RespType::Boolean:
    return asBoolean() ? "true" : "false";
  case RespType::Double:
    return asString();
  case RespType::BigNumber:
    return fmt::format("(big){}", asString());
  case RespType::BlobError:
    return fmt::format("!({})", asString());
  case RespType::VerbatimString:
    return fmt::format("={}", asString());
  case RespType::Map: {
    std::string ret = "{";
    const auto& arr = asArray();
    for (uint64_t i = 0; i < arr.size(); i += 2) {
      ret += arr[i].toString();
      ret += ": ";
      if (i + 1 < arr.size()) {
        ret += arr[i + 1].toString();
      }
      if (i + 2 < arr.size()) {
        ret += ", ";
      }
    }
    return ret + "}";
  }
  case RespType::Set:
    return joinAggregate("~", asArray());
  case RespType::Push:
    return joinAggregate(">", asArray());
  }

  return "";
}

std::vector<RespValue>& RespValue::asArray() {
  ASSERT(type_ == RespType::Array || type_ == RespType::Map || type_ == RespType::Set ||
         type_ == RespType::Push);
  return array_;
}

const std::vector<RespValue>& RespValue::asArray() const {
  ASSERT(type_ == RespType::Array || type_ == RespType::Map || type_ == RespType::Set ||
         type_ == RespType::Push);
  return array_;
}

std::string& RespValue::asString() {
  ASSERT(type_ == RespType::BulkString || type_ == RespType::Error ||
         type_ == RespType::SimpleString || type_ == RespType::BlobError ||
         type_ == RespType::VerbatimString || type_ == RespType::BigNumber ||
         type_ == RespType::Double);
  return string_;
}

const std::string& RespValue::asString() const {
  ASSERT(type_ == RespType::BulkString || type_ == RespType::Error ||
         type_ == RespType::SimpleString || type_ == RespType::BlobError ||
         type_ == RespType::VerbatimString || type_ == RespType::BigNumber ||
         type_ == RespType::Double);
  return string_;
}

int64_t& RespValue::asInteger() {
  ASSERT(type_ == RespType::Integer || type_ == RespType::Boolean);
  return integer_;
}

int64_t RespValue::asInteger() const {
  ASSERT(type_ == RespType::Integer || type_ == RespType::Boolean);
  return integer_;
}

RespValue::CompositeArray& RespValue::asCompositeArray() {
  ASSERT(type_ == RespType::CompositeArray);
  return composite_array_;
}

const RespValue::CompositeArray& RespValue::asCompositeArray() const {
  ASSERT(type_ == RespType::CompositeArray);
  return composite_array_;
}

bool RespValue::asBoolean() const {
  ASSERT(type_ == RespType::Boolean);
  return integer_ != 0;
}

void RespValue::cleanup() {
  // Need to manually delete because of the union.
  switch (type_) {
  case RespType::Array:
  case RespType::Map:
  case RespType::Set:
  case RespType::Push: {
    array_.~vector<RespValue>();
    break;
  }
  case RespType::CompositeArray: {
    composite_array_.~CompositeArray();
    break;
  }
  case RespType::SimpleString:
  case RespType::BulkString:
  case RespType::Error:
  case RespType::BlobError:
  case RespType::VerbatimString:
  case RespType::BigNumber:
  case RespType::Double: {
    string_.~basic_string<char>();
    break;
  }
  case RespType::Null:
  case RespType::Integer:
  case RespType::Boolean: {
    break;
  }
  }
}

void RespValue::type(RespType type) {
  cleanup();

  // Need to use placement new because of the union.
  type_ = type;
  switch (type) {
  case RespType::Array:
  case RespType::Map:
  case RespType::Set:
  case RespType::Push: {
    new (&array_) std::vector<RespValue>();
    break;
  }
  case RespType::CompositeArray: {
    new (&composite_array_) CompositeArray();
    break;
  }
  case RespType::SimpleString:
  case RespType::BulkString:
  case RespType::Error:
  case RespType::BlobError:
  case RespType::VerbatimString:
  case RespType::BigNumber:
  case RespType::Double: {
    new (&string_) std::string();
    break;
  }
  case RespType::Null:
    break;
  case RespType::Integer:
  case RespType::Boolean: {
    integer_ = 0;
    break;
  }
  }
}

RespValue::RespValue(const RespValue& other) : type_(RespType::Null) {
  type(other.type());
  switch (type_) {
  case RespType::Array:
  case RespType::Map:
  case RespType::Set:
  case RespType::Push: {
    asArray() = other.asArray();
    break;
  }
  case RespType::CompositeArray: {
    asCompositeArray() = other.asCompositeArray();
    break;
  }
  case RespType::SimpleString:
  case RespType::BulkString:
  case RespType::Error:
  case RespType::BlobError:
  case RespType::VerbatimString:
  case RespType::BigNumber:
  case RespType::Double: {
    asString() = other.asString();
    break;
  }
  case RespType::Integer:
  case RespType::Boolean: {
    asInteger() = other.asInteger();
    break;
  }
  case RespType::Null:
    break;
  }
}

RespValue::RespValue(RespValue&& other) noexcept : type_(other.type_) {
  switch (type_) {
  case RespType::Array:
  case RespType::Map:
  case RespType::Set:
  case RespType::Push: {
    new (&array_) std::vector<RespValue>(std::move(other.array_));
    break;
  }
  case RespType::CompositeArray: {
    new (&composite_array_) CompositeArray(std::move(other.composite_array_));
    break;
  }
  case RespType::SimpleString:
  case RespType::BulkString:
  case RespType::Error:
  case RespType::BlobError:
  case RespType::VerbatimString:
  case RespType::BigNumber:
  case RespType::Double: {
    new (&string_) std::string(std::move(other.string_));
    break;
  }
  case RespType::Integer:
  case RespType::Boolean: {
    integer_ = other.integer_;
    break;
  }
  case RespType::Null:
    break;
  }
}

RespValue& RespValue::operator=(const RespValue& other) {
  if (&other == this) {
    return *this;
  }
  type(other.type());
  switch (type_) {
  case RespType::Array:
  case RespType::Map:
  case RespType::Set:
  case RespType::Push: {
    asArray() = other.asArray();
    break;
  }
  case RespType::CompositeArray: {
    asCompositeArray() = other.asCompositeArray();
    break;
  }
  case RespType::SimpleString:
  case RespType::BulkString:
  case RespType::Error:
  case RespType::BlobError:
  case RespType::VerbatimString:
  case RespType::BigNumber:
  case RespType::Double: {
    asString() = other.asString();
    break;
  }
  case RespType::Integer:
  case RespType::Boolean: {
    asInteger() = other.asInteger();
    break;
  }
  case RespType::Null:
    break;
  }
  return *this;
}

RespValue& RespValue::operator=(RespValue&& other) noexcept {
  if (&other == this) {
    return *this;
  }

  type(other.type());
  switch (type_) {
  case RespType::Array:
  case RespType::Map:
  case RespType::Set:
  case RespType::Push: {
    array_ = std::move(other.array_);
    break;
  }
  case RespType::CompositeArray: {
    composite_array_ = std::move(other.composite_array_);
    break;
  }
  case RespType::SimpleString:
  case RespType::BulkString:
  case RespType::Error:
  case RespType::BlobError:
  case RespType::VerbatimString:
  case RespType::BigNumber:
  case RespType::Double: {
    string_ = std::move(other.string_);
    break;
  }
  case RespType::Integer:
  case RespType::Boolean: {
    integer_ = other.integer_;
    break;
  }
  case RespType::Null:
    break;
  }
  return *this;
}

bool RespValue::operator==(const RespValue& other) const {
  bool result = false;
  if (type_ != other.type()) {
    return result;
  }

  switch (type_) {
  case RespType::Array:
  case RespType::Map:
  case RespType::Set:
  case RespType::Push: {
    result = (asArray() == other.asArray());
    break;
  }
  case RespType::CompositeArray: {
    result = (asCompositeArray() == other.asCompositeArray());
    break;
  }
  case RespType::SimpleString:
  case RespType::BulkString:
  case RespType::Error:
  case RespType::BlobError:
  case RespType::VerbatimString:
  case RespType::BigNumber:
  case RespType::Double: {
    // Double compares byte-for-byte on the stored payload — stricter than IEEE double
    // equality (two payloads representing the same numeric value compare unequal).
    result = (asString() == other.asString());
    break;
  }
  case RespType::Integer:
  case RespType::Boolean: {
    result = (asInteger() == other.asInteger());
    break;
  }
  case RespType::Null: {
    result = true;
    break;
  }
  }
  return result;
}

uint64_t RespValue::CompositeArray::size() const {
  return (command_ && base_array_) ? end_ - start_ + 2 : 0;
}

bool RespValue::CompositeArray::operator==(const RespValue::CompositeArray& other) const {
  return base_array_ == other.base_array_ && command_ == other.command_ && start_ == other.start_ &&
         end_ == other.end_;
}

const RespValue& RespValue::CompositeArray::CompositeArrayConstIterator::operator*() {
  return first_ ? *command_ : array_[index_];
}

RespValue::CompositeArray::CompositeArrayConstIterator&
RespValue::CompositeArray::CompositeArrayConstIterator::operator++() {
  if (first_) {
    first_ = false;
  } else {
    ++index_;
  }
  return *this;
}

bool RespValue::CompositeArray::CompositeArrayConstIterator::operator!=(
    const CompositeArrayConstIterator& rhs) const {
  return command_ != (rhs.command_) || &array_ != &(rhs.array_) || index_ != rhs.index_ ||
         first_ != rhs.first_;
}

const RespValue::CompositeArray::CompositeArrayConstIterator&
RespValue::CompositeArray::CompositeArrayConstIterator::empty() {
  static const RespValue::CompositeArray::CompositeArrayConstIterator* instance =
      new RespValue::CompositeArray::CompositeArrayConstIterator(nullptr, {}, 0, false);
  return *instance;
}

void DecoderImpl::decode(Buffer::Instance& data) {
  for (const Buffer::RawSlice& slice : data.getRawSlices()) {
    parseSlice(slice);
  }

  data.drain(data.length());
}

void DecoderImpl::parseSlice(const Buffer::RawSlice& slice) {
  const char* buffer = reinterpret_cast<const char*>(slice.mem_);
  uint64_t remaining = slice.len_;

  while (remaining || state_ == State::ValueComplete) {
    ENVOY_LOG(trace, "parse slice: {} remaining", remaining);
    switch (state_) {
    case State::ValueRootStart: {
      ENVOY_LOG(trace, "parse slice: ValueRootStart");
      // Reset the cumulative-element budget for a new top-level value.
      // Nested aggregates within this value share the same budget; a fresh
      // pipelined request gets a fresh budget.
      total_elements_ = 0;
      pending_value_root_ = std::make_unique<RespValue>();
      pending_value_stack_.push_front({pending_value_root_.get(), 0});
      pending_value_stack_depth_ = 1;
      const char c = buffer[0];
      if (std::isalnum(c) || std::isspace(c) || c == '"' || c == '\'') {
        pending_value_stack_.front().value_->type(RespType::Array);
        state_ = State::InlineStart;
      } else {
        state_ = State::ValueStart;
      }
      break;
    }

    case State::ValueStart: {
      ENVOY_LOG(trace, "parse slice: ValueStart: {}", buffer[0]);
      pending_integer_.reset();
      switch (buffer[0]) {
      case '*': {
        state_ = State::IntegerStart;
        pending_value_stack_.front().value_->type(RespType::Array);
        break;
      }
      case '$': {
        state_ = State::IntegerStart;
        pending_value_stack_.front().value_->type(RespType::BulkString);
        break;
      }
      case '-': {
        state_ = State::SimpleString;
        pending_value_stack_.front().value_->type(RespType::Error);
        break;
      }
      case '+': {
        state_ = State::SimpleString;
        pending_value_stack_.front().value_->type(RespType::SimpleString);
        break;
      }
      case ':': {
        state_ = State::IntegerStart;
        pending_value_stack_.front().value_->type(RespType::Integer);
        break;
      }
      case '_': { // RESP3 Null
        pending_value_stack_.front().value_->type(RespType::Null);
        state_ = State::CR;
        break;
      }
      case '#': { // RESP3 Boolean
        state_ = State::BooleanValue;
        pending_value_stack_.front().value_->type(RespType::Boolean);
        break;
      }
      case ',': { // RESP3 Double - reuse SimpleString accumulator; payload validated and
                  // moved into RespValue::asString() at ValueComplete (pass-through).
        state_ = State::SimpleString;
        pending_double_buf_.clear();
        pending_value_stack_.front().value_->type(RespType::Double);
        break;
      }
      case '(': { // RESP3 BigNumber - reuse SimpleString path
        state_ = State::SimpleString;
        pending_value_stack_.front().value_->type(RespType::BigNumber);
        break;
      }
      case '!': { // RESP3 BlobError - reuse BulkString path (length-prefixed)
        state_ = State::IntegerStart;
        pending_value_stack_.front().value_->type(RespType::BlobError);
        break;
      }
      case '=': { // RESP3 VerbatimString - reuse BulkString path (length-prefixed)
        state_ = State::IntegerStart;
        pending_value_stack_.front().value_->type(RespType::VerbatimString);
        break;
      }
      case '%': { // RESP3 Map
        state_ = State::IntegerStart;
        pending_value_stack_.front().value_->type(RespType::Map);
        break;
      }
      case '~': { // RESP3 Set
        state_ = State::IntegerStart;
        pending_value_stack_.front().value_->type(RespType::Set);
        break;
      }
      case '>': { // RESP3 Push
        state_ = State::IntegerStart;
        pending_value_stack_.front().value_->type(RespType::Push);
        break;
      }
      case '|': { // RESP3 Attribute - parse and discard (same wire format as Map)
        if (++consecutive_attributes_ > kMaxConsecutiveAttributes) {
          throw ProtocolError("too many consecutive RESP3 attributes");
        }
        state_ = State::IntegerStart;
        pending_value_stack_.front().value_->type(RespType::Map);
        pending_value_stack_.front().is_attribute_ = true;
        ++open_attribute_frames_;
        break;
      }
      default: {
        throw ProtocolError("invalid value type");
      }
      }

      remaining--;
      buffer++;
      break;
    }

    case State::InlineStart: {
      ENVOY_LOG(trace, "parse slice: InlineStart: {}", buffer[0]);
      if (buffer[0] == '\r') {
        state_ = State::LF;
      } else if (std::isspace(buffer[0])) {
        // Discard whitespace
      } else {
        RespValuePtr pending_value = std::make_unique<RespValue>();
        pending_value->type(RespType::BulkString);

        if (buffer[0] == '"') {
          state_ = State::InlineStringQuoted;
        } else if (buffer[0] == '\'') {
          state_ = State::InlineStringSingleQuoted;
        } else {
          pending_value->asString().push_back(buffer[0]);
          state_ = State::InlineString;
        }

        // Inline commands carry no element-count header, so the per-aggregate cap that
        // ``*``-framed arrays get at IntegerLF must be enforced per appended word here —
        // otherwise an endless space-separated word stream (no CRLF) allocates one RespValue
        // per word without bound.
        if (pending_value_stack_.front().value_->asArray().size() >= kMaxRespElements) {
          throw ProtocolError("inline command element count exceeds maximum");
        }
        size_t n = pending_value_stack_.front().value_->asArray().size();
        pending_value_stack_.front().value_->asArray().push_back(*pending_value);
        pending_value_stack_.push_front({&pending_value_stack_.front().value_->asArray()[n], n});
        ++pending_value_stack_depth_;
      }

      remaining--;
      buffer++;
      break;
    }

    case State::InlineDelimiter: {
      ENVOY_LOG(trace, "parse slice: InlineDelimiter: {}", buffer[0]);
      if (buffer[0] == '\r') {
        state_ = State::LF;
      } else if (std::isspace(buffer[0])) {
        state_ = State::InlineStart;
      } else {
        throw ProtocolError("unbalanced quotes in request");
      }

      remaining--;
      buffer++;
      break;
    }

    case State::InlineString: {
      ENVOY_LOG(trace, "parse slice: InlineString: {}", buffer[0]);

      if (buffer[0] == '\r') {
        pending_value_stack_.pop_front();
        --pending_value_stack_depth_;
        state_ = State::LF;
      } else if (std::isspace(buffer[0])) {
        pending_value_stack_.pop_front();
        --pending_value_stack_depth_;
        state_ = State::InlineStart;
      } else if (buffer[0] == '"') {
        throw ProtocolError("unbalanced quotes in request");
      } else {
        pending_value_stack_.front().value_->asString().push_back(buffer[0]);
      }

      remaining--;
      buffer++;
      break;
    }

    case State::InlineStringQuoted: {
      ENVOY_LOG(trace, "parse slice: InlineStringQuoted: {}", buffer[0]);

      if (buffer[0] == '\r') {
        throw ProtocolError("unbalanced quotes in request");
      } else if (buffer[0] == '"') {
        pending_value_stack_.pop_front();
        --pending_value_stack_depth_;
        state_ = State::InlineDelimiter;
      } else if (buffer[0] == '\\') {
        state_ = State::InlineStringQuotedEscape;
      } else {
        pending_value_stack_.front().value_->asString().push_back(buffer[0]);
      }

      remaining--;
      buffer++;
      break;
    }

    case State::InlineStringQuotedEscape: {
      ENVOY_LOG(trace, "parse slice: InlineStringQuotedEscape: {}", buffer[0]);

      if (buffer[0] == '\r') {
        throw ProtocolError("unbalanced quotes in request");
      } else if (buffer[0] == 'x') {
        state_ = State::InlineStringQuotedEscapeHex;
        pending_value_stack_.front().value_->asString().push_back(buffer[0]);
      } else {
        char c;
        switch (buffer[0]) {
        case '\\':
          c = '\\';
          break;
        case 'n':
          c = '\n';
          break;
        case 'r':
          c = '\r';
          break;
        case 't':
          c = '\t';
          break;
        case 'b':
          c = '\b';
          break;
        case 'a':
          c = '\a';
          break;
        default:
          c = buffer[0];
          break;
        }
        pending_value_stack_.front().value_->asString().push_back(c);
        state_ = State::InlineStringQuoted;
      }

      remaining--;
      buffer++;
      break;
    }

    case State::InlineStringQuotedEscapeHex: {
      ENVOY_LOG(trace, "parse slice: InlineStringQuotedEscapeHex: {}", buffer[0]);

      if (!std::isxdigit(buffer[0])) {
        state_ = State::InlineStringQuoted;
        break;
      }

      auto& s = pending_value_stack_.front().value_->asString();
      ASSERT((!s.empty() && s.back() == 'x') || (s.size() > 1 && s[s.size() - 2] == 'x'));
      s.push_back(buffer[0]);
      if (s[s.size() - 3] == 'x') {
        char c = static_cast<char>(std::stoul(&s[s.size() - 2], nullptr, 16));
        s.resize(s.size() - 3);
        s.push_back(c);
        state_ = State::InlineStringQuoted;
      }

      remaining--;
      buffer++;
      break;
    }

    case State::InlineStringSingleQuoted: {
      ENVOY_LOG(trace, "parse slice: InlineStringSingleQuoted: {}", buffer[0]);

      if (buffer[0] == '\r') {
        throw ProtocolError("unbalanced quotes in request");
      } else if (buffer[0] == '\'') {
        pending_value_stack_.pop_front();
        --pending_value_stack_depth_;
        state_ = State::InlineDelimiter;
      } else if (buffer[0] == '\\') {
        pending_value_stack_.front().value_->asString().push_back('\\');
        state_ = State::InlineStringSingleQuotedEscape;
      } else {
        pending_value_stack_.front().value_->asString().push_back(buffer[0]);
      }

      remaining--;
      buffer++;
      break;
    }

    case State::InlineStringSingleQuotedEscape: {
      ENVOY_LOG(trace, "parse slice: InlineStringSingleQuoted: {}", buffer[0]);

      if (buffer[0] != '\'') {
        state_ = State::InlineStringSingleQuoted;
        break;
      }

      auto& s = pending_value_stack_.front().value_->asString();
      ASSERT(!s.empty() && s.back() == '\\');
      s.pop_back();
      s.push_back(buffer[0]);

      state_ = State::InlineStringSingleQuoted;
      remaining--;
      buffer++;
      break;
    }

    case State::IntegerStart: {
      ENVOY_LOG(trace, "parse slice: IntegerStart: {}", buffer[0]);
      if (buffer[0] == '-') {
        pending_integer_.negative_ = true;
        remaining--;
        buffer++;
      }

      state_ = State::Integer;
      break;
    }

    case State::Integer: {
      ENVOY_LOG(trace, "parse slice: Integer: {}", buffer[0]);
      char c = buffer[0];
      if (buffer[0] == '\r') {
        state_ = State::IntegerLF;
      } else {
        if (c < '0' || c > '9') {
          throw ProtocolError("invalid integer character");
        } else {
          const uint64_t next_digit = c - '0';
          if (pending_integer_.integer_ > std::numeric_limits<uint64_t>::max() / 10 ||
              (pending_integer_.integer_ == std::numeric_limits<uint64_t>::max() / 10 &&
               next_digit > std::numeric_limits<uint64_t>::max() % 10)) {
            throw ProtocolError("integer overflow");
          }
          pending_integer_.integer_ = (pending_integer_.integer_ * 10) + next_digit;
          pending_integer_.digit_seen_ = true;
        }
      }

      remaining--;
      buffer++;
      break;
    }

    case State::IntegerLF: {
      if (buffer[0] != '\n') {
        throw ProtocolError("expected new line");
      }

      // Reject integer/count/length lines that carry no decimal digits between the type byte
      // (and optional sign) and ``\r\n``. Without this, ``:\r\n``, ``:-\r\n``, ``*\r\n``,
      // ``$\r\n``, ``%-\r\n`` etc. would all be silently accepted as zero by the dispatch
      // below — indistinguishable on the wire from a legitimate zero-valued frame. See
      // PendingInteger::digit_seen_ for full rationale.
      if (!pending_integer_.digit_seen_) {
        throw ProtocolError("integer with no digits");
      }

      ENVOY_LOG(trace, "parse slice: IntegerLF: {}", pending_integer_.integer_);
      remaining--;
      buffer++;

      PendingValue& current_value = pending_value_stack_.front();
      RespType value_type = current_value.value_->type();
      if (value_type == RespType::Array || value_type == RespType::Set ||
          value_type == RespType::Push) {
        if (pending_integer_.negative_) {
          // Per RESP spec, only Array has a null form (``*-1``). Any other
          // negative count for Array, and ANY negative count for Set or
          // Push (which have no null variant), is malformed and must be
          // rejected — accepting them would silently treat e.g. ``*-2`` as
          // null and let an attacker inject ambiguous frames.
          if (value_type == RespType::Array && pending_integer_.integer_ == 1) {
            current_value.value_->type(RespType::Null);
            state_ = State::ValueComplete;
          } else {
            throw ProtocolError("invalid negative count");
          }
        } else if (pending_integer_.integer_ == 0) {
          state_ = State::ValueComplete;
        } else {
          if (pending_integer_.integer_ > kMaxRespElements) {
            throw ProtocolError("element count exceeds maximum");
          }
          // Cumulative-element budget: defeats DoS via deep nesting whose
          // total element count multiplies (e.g. *1024 of *1024 of *1024 ...)
          // where each level individually satisfies kMaxRespElements but the
          // product is unbounded.
          if (pending_integer_.integer_ > kMaxTotalElements - total_elements_) {
            throw ProtocolError("total element count exceeds maximum");
          }
          total_elements_ += pending_integer_.integer_;
          // Bound nesting depth BEFORE the push so the post-push depth can never exceed
          // kMaxNestingDepth. The counter includes the root frame, so a depth already equal
          // to kMaxNestingDepth is at the cap and the next push is rejected here.
          if (pending_value_stack_depth_ >= kMaxNestingDepth) {
            throw ProtocolError("nesting depth exceeds maximum");
          }
          std::vector<RespValue> values(pending_integer_.integer_);
          current_value.value_->asArray().swap(values);
          pending_value_stack_.push_front({&current_value.value_->asArray()[0], 0});
          ++pending_value_stack_depth_;
          state_ = State::ValueStart;
        }
      } else if (value_type == RespType::Map) {
        if (pending_integer_.negative_) {
          // RESP3 Map has no null form — reject all negative counts. The
          // RESP3 ``_\r\n`` null is a separate type byte; null cannot be
          // smuggled in via ``%-1`` etc.
          throw ProtocolError("invalid negative count");
        } else if (pending_integer_.integer_ == 0) {
          state_ = State::ValueComplete;
        } else {
          // Map wire format: count = N key-value pairs, stored as 2*N array elements.
          if (pending_integer_.integer_ > std::numeric_limits<uint64_t>::max() / 2) {
            throw ProtocolError("map element count overflow");
          }
          uint64_t element_count = pending_integer_.integer_ * 2;
          if (element_count > kMaxRespElements) {
            throw ProtocolError("element count exceeds maximum");
          }
          if (element_count > kMaxTotalElements - total_elements_) {
            throw ProtocolError("total element count exceeds maximum");
          }
          total_elements_ += element_count;
          // Pre-push nesting check (see Array branch above).
          if (pending_value_stack_depth_ >= kMaxNestingDepth) {
            throw ProtocolError("nesting depth exceeds maximum");
          }
          std::vector<RespValue> values(element_count);
          current_value.value_->asArray().swap(values);
          pending_value_stack_.push_front({&current_value.value_->asArray()[0], 0});
          ++pending_value_stack_depth_;
          state_ = State::ValueStart;
        }
      } else if (value_type == RespType::Integer) {
        // Enforce signed int64 range so wire integers outside [INT64_MIN,
        // INT64_MAX] are rejected at parse time rather than silently wrapped
        // into asInteger() (which is int64_t). Accumulator widths:
        //   positive: pending_integer_.integer_ in [0, INT64_MAX]
        //   negative: pending_integer_.integer_ in [1, INT64_MAX + 1]
        //             (the upper bound represents -INT64_MIN = 2^63).
        constexpr uint64_t kMaxPositive =
            static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
        constexpr uint64_t kMaxNegativeMagnitude = kMaxPositive + 1;
        if (!pending_integer_.negative_) {
          if (pending_integer_.integer_ > kMaxPositive) {
            throw ProtocolError("integer out of range");
          }
          current_value.value_->asInteger() = static_cast<int64_t>(pending_integer_.integer_);
        } else if (pending_integer_.integer_ == 0) {
          // "-0" → 0. Tolerate the wire form; map to plain zero.
          current_value.value_->asInteger() = 0;
        } else {
          if (pending_integer_.integer_ > kMaxNegativeMagnitude) {
            throw ProtocolError("integer out of range");
          }
          // Subtract 1 before negating so -INT64_MIN (= 2^63, unrepresentable
          // as int64_t) does not overflow the intermediate cast: range
          // [1, 2^63] − 1 → [0, 2^63 − 1] which fits in int64_t.
          current_value.value_->asInteger() =
              static_cast<int64_t>(pending_integer_.integer_ - 1) * -1 - 1;
        }
        state_ = State::ValueComplete;
      } else {
        ASSERT(value_type == RespType::BulkString || value_type == RespType::BlobError ||
               value_type == RespType::VerbatimString);
        if (!pending_integer_.negative_) {
          // Cap length BEFORE the body-read loop. The body loop appends
          // length_to_copy bytes per slice into asString(), so an
          // attacker-controlled length header would otherwise drive
          // unbounded string growth on a single message. The 512 MiB cap
          // matches Redis's own proto-max-bulk-len default.
          if (pending_integer_.integer_ > kMaxBulkStringLength) {
            throw ProtocolError("bulk string length exceeds maximum");
          }
          state_ = State::BulkStringBody;
        } else if (value_type == RespType::BulkString && pending_integer_.integer_ == 1) {
          // Per RESP spec, only ``$-1`` is the null bulk string. Anything
          // else negative (``$-2``, ``$-3``, ...) is malformed and must be
          // rejected — accepting them would silently convert ambiguous
          // frames into null. VerbatimString and BlobError have no null
          // form at all and reject any negative length below.
          current_value.value_->type(RespType::Null);
          state_ = State::ValueComplete;
        } else {
          throw ProtocolError("invalid negative length");
        }
      }

      break;
    }

    case State::BulkStringBody: {
      ASSERT(!pending_integer_.negative_);
      uint64_t length_to_copy =
          std::min(static_cast<uint64_t>(pending_integer_.integer_), remaining);
      pending_value_stack_.front().value_->asString().append(buffer, length_to_copy);
      pending_integer_.integer_ -= length_to_copy;
      remaining -= length_to_copy;
      buffer += length_to_copy;

      if (pending_integer_.integer_ == 0) {
        ENVOY_LOG(trace, "parse slice: BulkStringBody complete: {}",
                  pending_value_stack_.front().value_->asString());
        if (pending_value_stack_.front().value_->type() == RespType::VerbatimString &&
            !isValidResp3VerbatimString(pending_value_stack_.front().value_->asString())) {
          throw ProtocolError("invalid verbatim string value");
        }
        state_ = State::CR;
      }

      break;
    }

    case State::CR: {
      ENVOY_LOG(trace, "parse slice: CR");
      if (buffer[0] != '\r') {
        throw ProtocolError("expected carriage return");
      }

      remaining--;
      buffer++;
      state_ = State::LF;
      break;
    }

    case State::LF: {
      ENVOY_LOG(trace, "parse slice: LF");
      if (buffer[0] != '\n') {
        throw ProtocolError("expected new line");
      }

      remaining--;
      buffer++;
      state_ = State::ValueComplete;
      break;
    }

    case State::SimpleString: {
      ENVOY_LOG(trace, "parse slice: SimpleString: {}", buffer[0]);
      if (buffer[0] == '\r') {
        // For Double, validate as parseable and move into ``string_`` (pass-through).
        RespValue& current_value = *pending_value_stack_.front().value_;
        if (current_value.type() == RespType::Double) {
          double result;
          if (!absl::SimpleAtod(pending_double_buf_, &result)) {
            throw ProtocolError("invalid double value");
          }
          current_value.asString() = std::move(pending_double_buf_);
          pending_double_buf_.clear();
        } else if (current_value.type() == RespType::BigNumber &&
                   !isValidResp3BigNumber(current_value.asString())) {
          throw ProtocolError("invalid big number value");
        }
        state_ = State::LF;
      } else {
        // Double accumulates into the cap-guarded scratch buffer; everything else appends
        // directly to its asString(). Double and BigNumber are length-capped to bound
        // unbounded CRLF-less line growth (Double tightly; BigNumber generously since it is
        // arbitrary-precision).
        RespValue& current_value = *pending_value_stack_.front().value_;
        if (current_value.type() == RespType::Double) {
          // Reject whitespace and non-printable bytes up front. ``SimpleAtod`` tolerates
          // surrounding ASCII whitespace, so without this an embedded space/tab (or, since
          // only ``\r`` terminates this state, a bare ``\n``) would validate and then be
          // re-emitted verbatim inside a line-framed RESP3 Double, which would desynchronize
          // strict downstream parsers. Legitimate Double tokens are printable, space-free ASCII
          // (digits, sign, '.', 'e'/'E', "inf"/"nan").
          const unsigned char c = static_cast<unsigned char>(buffer[0]);
          if (absl::ascii_isspace(c) || !absl::ascii_isprint(c)) {
            throw ProtocolError("invalid double value");
          }
          if (pending_double_buf_.size() >= kMaxDoubleTokenLength) {
            throw ProtocolError("double value too long");
          }
          pending_double_buf_.push_back(buffer[0]);
        } else {
          // SimpleString and Error grow their asString() uncapped here. Unlike Double/BigNumber
          // (whose per-token caps bound a scalar that a small buffer easily holds), these carry
          // arbitrary-length text, and their growth is strictly 1:1 with attacker bytes already
          // counted against the connection read buffer — there is no multiplicative or
          // cumulative amplification for a cap to defend against, so upstream leaves them
          // uncapped and we match.
          if (current_value.type() == RespType::BigNumber &&
              current_value.asString().size() >= kMaxBigNumberTokenLength) {
            throw ProtocolError("big number value too long");
          }
          current_value.asString().push_back(buffer[0]);
        }
      }

      remaining--;
      buffer++;
      break;
    }

    case State::BooleanValue: {
      ENVOY_LOG(trace, "parse slice: BooleanValue: {}", buffer[0]);
      if (buffer[0] == 't') {
        pending_value_stack_.front().value_->asInteger() = 1;
      } else if (buffer[0] == 'f') {
        pending_value_stack_.front().value_->asInteger() = 0;
      } else {
        throw ProtocolError("invalid boolean value");
      }
      state_ = State::CR;
      remaining--;
      buffer++;
      break;
    }

    case State::ValueComplete: {
      ENVOY_LOG(trace, "parse slice: ValueComplete");
      ASSERT(!pending_value_stack_.empty());
      bool was_attribute = pending_value_stack_.front().is_attribute_;
      if (was_attribute) {
        --open_attribute_frames_;
      } else if (open_attribute_frames_ == 0) {
        // Only a value completing OUTSIDE any attribute counts as progress for the
        // consecutive-attribute guard. Children completing inside an attribute belong to a
        // frame that is itself discarded; letting them reset the counter would neuter the cap
        // for every non-empty attribute (``|1 <k> <v>`` repeated indefinitely).
        consecutive_attributes_ = 0;
      }
      pending_value_stack_.pop_front();
      --pending_value_stack_depth_;
      if (pending_value_stack_.empty()) {
        if (was_attribute) {
          // Root-level attribute discarded. Parse the actual value next. Discarding released
          // the attribute's element storage, so the following value gets a fresh cumulative
          // budget (same as ValueRootStart) — a legitimately large value must not be rejected
          // because a large attribute preceded it.
          total_elements_ = 0;
          pending_value_root_ = std::make_unique<RespValue>();
          pending_value_stack_.push_front({pending_value_root_.get(), 0});
          pending_value_stack_depth_ = 1;
          state_ = State::ValueStart;
        } else {
          callbacks_.onRespValue(std::move(pending_value_root_));
          state_ = State::ValueRootStart;
        }
      } else {
        PendingValue& current_value = pending_value_stack_.front();
        ASSERT(current_value.value_->type() == RespType::Array ||
               current_value.value_->type() == RespType::Map ||
               current_value.value_->type() == RespType::Set ||
               current_value.value_->type() == RespType::Push);
        if (was_attribute) {
          // Attribute discarded inside a compound type. Reuse the same slot
          // for the actual value that the attribute annotates.
          RespValue* slot = &current_value.value_->asArray()[current_value.current_array_element_];
          *slot = RespValue();
          pending_value_stack_.push_front({slot, 0});
          ++pending_value_stack_depth_;
          state_ = State::ValueStart;
        } else if (current_value.current_array_element_ <
                   current_value.value_->asArray().size() - 1) {
          current_value.current_array_element_++;
          pending_value_stack_.push_front(
              {&current_value.value_->asArray()[current_value.current_array_element_], 0});
          ++pending_value_stack_depth_;
          state_ = State::ValueStart;
        }
      }

      break;
    }
    }
  }
}

void EncoderImpl::encode(const RespValue& value, Buffer::Instance& out) {
  switch (value.type()) {
  case RespType::Array: {
    encodeArray(value.asArray(), out);
    break;
  }
  case RespType::CompositeArray: {
    encodeCompositeArray(value.asCompositeArray(), out);
    break;
  }
  case RespType::SimpleString: {
    encodeSimpleString(value.asString(), out);
    break;
  }
  case RespType::BulkString: {
    encodeBulkString(value.asString(), out);
    break;
  }
  case RespType::Error: {
    encodeError(value.asString(), out);
    break;
  }
  case RespType::Null: {
    if (protocol_version_ == RespProtocolVersion::Resp3) {
      encodeNull(out);
    } else {
      out.add("$-1\r\n", 5);
    }
    break;
  }
  case RespType::Integer: {
    encodeInteger(value.asInteger(), out);
    break;
  }
  case RespType::Boolean: {
    // RESP2 has no native boolean — emit integer 1/0. This matches
    // the RESP2-compat table in the RESP3 spec.
    if (protocol_version_ == RespProtocolVersion::Resp3) {
      encodeBoolean(value.asBoolean(), out);
    } else {
      encodeInteger(value.asBoolean() ? 1 : 0, out);
    }
    break;
  }
  case RespType::Double: {
    // Pass-through; RESP2 has no native Double so the payload is emitted as a bulk string.
    if (protocol_version_ == RespProtocolVersion::Resp3) {
      out.add(",", 1);
      out.add(value.asString());
      out.add("\r\n", 2);
    } else {
      encodeBulkString(value.asString(), out);
    }
    break;
  }
  case RespType::BigNumber: {
    // RESP2 has no native big number — emit a bulk string containing
    // the digit string.
    if (protocol_version_ == RespProtocolVersion::Resp3) {
      encodeBigNumber(value.asString(), out);
    } else {
      encodeBulkString(value.asString(), out);
    }
    break;
  }
  case RespType::BlobError: {
    // RESP2 inline error has no length prefix, so embedded CR/LF in the message
    // would desynchronize the parser. sanitizeControlBytes() replaces those (and
    // any other control byte) with spaces. RESP3 blob error is length-prefixed
    // and tolerates embedded CR/LF.
    if (protocol_version_ == RespProtocolVersion::Resp3) {
      encodeBlobError(value.asString(), out);
    } else {
      encodeError(sanitizeControlBytes(value.asString()), out);
    }
    break;
  }
  case RespType::VerbatimString: {
    // RESP3 verbatim string wire form is "=<len>\r\n<xxx>:<data>\r\n" (3-char
    // format prefix + colon + data). RESP2 has no native verbatim — emit a
    // plain bulk string carrying just the data portion. The decoder already
    // rejects malformed verbatim strings via isValidResp3VerbatimString(), so
    // we can rely on the "xxx:" prefix being present.
    if (protocol_version_ == RespProtocolVersion::Resp3) {
      encodeVerbatimString(value.asString(), out);
    } else {
      const std::string& s = value.asString();
      // isValidResp3VerbatimString requires at least "xxx:" (4 bytes) before data.
      // We defensively handle shorter inputs by emitting empty bulk string.
      if (s.size() > 4) {
        encodeBulkString(s.substr(4), out);
      } else {
        encodeBulkString("", out);
      }
    }
    break;
  }
  case RespType::Map: {
    // Storage is flat 2*N [k0,v0,k1,v1,...]. RESP3 wire count is N pairs (%N); RESP2 emits the
    // flat array as-is (*2N) since RESP2 has no map type. Both share the even-length invariant,
    // so enforce it once here and hand both paths the same (truncated-to-even) view — otherwise
    // an odd caller vector would frame differently per negotiated version (RESP3 dropped the
    // stray element while RESP2 emitted it).
    const std::vector<RespValue>& stored = value.asArray();
    ENVOY_BUG(stored.size() % 2 == 0, "Map storage must have even length");
    const absl::Span<const RespValue> pairs(stored.data(), stored.size() & ~static_cast<size_t>(1));
    if (protocol_version_ == RespProtocolVersion::Resp3) {
      encodeMap(pairs, out);
    } else {
      encodeArray(pairs, out);
    }
    break;
  }
  case RespType::Set: {
    // RESP3 set (~N) is emitted as a RESP2 array (*N) on RESP2 targets — the
    // elements are already stored in a vector, and RESP2 clients treat the
    // result as a list whose duplicates (if any) are preserved, which is the
    // expected compat behavior.
    if (protocol_version_ == RespProtocolVersion::Resp3) {
      encodeSet(value.asArray(), out);
    } else {
      encodeArray(value.asArray(), out);
    }
    break;
  }
  case RespType::Push: {
    // RESP3-only frame. On RESP3 emit `>N` natively; on RESP2 down-convert to Array (`*N`)
    // since the RESP2 pubsub wire form is itself a bulk-string array — emitting `>N` to a
    // RESP2 reader would corrupt the next reply's framing.
    if (protocol_version_ == RespProtocolVersion::Resp3) {
      encodePush(value.asArray(), out);
    } else {
      encodeArray(value.asArray(), out);
    }
    break;
  }
  }
}

void EncoderImpl::encodeAggregate(char prefix, absl::Span<const RespValue> array,
                                  Buffer::Instance& out) {
  char buffer[32];
  char* current = buffer;
  *current++ = prefix;
  current += StringUtil::itoa(current, 21, array.size());
  *current++ = '\r';
  *current++ = '\n';
  out.add(buffer, current - buffer);

  for (const RespValue& value : array) {
    encode(value, out);
  }
}

void EncoderImpl::encodeArray(absl::Span<const RespValue> array, Buffer::Instance& out) {
  encodeAggregate('*', array, out);
}

void EncoderImpl::encodeCompositeArray(const RespValue::CompositeArray& composite_array,
                                       Buffer::Instance& out) {
  char buffer[32];
  char* current = buffer;
  *current++ = '*';
  current += StringUtil::itoa(current, 21, composite_array.size());
  *current++ = '\r';
  *current++ = '\n';
  out.add(buffer, current - buffer);
  for (const RespValue& value : composite_array) {
    encode(value, out);
  }
}

void EncoderImpl::encodeLengthPrefixed(char prefix, const std::string& string,
                                       Buffer::Instance& out) {
  char buffer[32];
  char* current = buffer;
  *current++ = prefix;
  current += StringUtil::itoa(current, 21, string.size());
  *current++ = '\r';
  *current++ = '\n';
  out.add(buffer, current - buffer);
  out.add(string);
  out.add("\r\n", 2);
}

void EncoderImpl::encodeBulkString(const std::string& string, Buffer::Instance& out) {
  encodeLengthPrefixed('$', string, out);
}

void EncoderImpl::encodeError(const std::string& string, Buffer::Instance& out) {
  out.add("-", 1);
  out.add(string);
  out.add("\r\n", 2);
}

void EncoderImpl::encodeInteger(int64_t integer, Buffer::Instance& out) {
  char buffer[32];
  char* current = buffer;
  *current++ = ':';
  if (integer >= 0) {
    current += StringUtil::itoa(current, 21, integer);
  } else {
    *current++ = '-';
    // By adding 1 (and later correcting) we ensure that we remain within the int64_t
    // range prior to the static_cast. This is an issue when we have a value of -2^63,
    // which cannot be represented as 2^63 in the intermediate int64_t.
    current += StringUtil::itoa(current, 30, static_cast<uint64_t>((integer + 1) * -1) + 1ULL);
  }

  *current++ = '\r';
  *current++ = '\n';
  out.add(buffer, current - buffer);
}

void EncoderImpl::encodeSimpleString(const std::string& string, Buffer::Instance& out) {
  out.add("+", 1);
  out.add(string);
  out.add("\r\n", 2);
}

void EncoderImpl::encodeNull(Buffer::Instance& out) { out.add("_\r\n", 3); }

void EncoderImpl::encodeBoolean(bool value, Buffer::Instance& out) {
  if (value) {
    out.add("#t\r\n", 4);
  } else {
    out.add("#f\r\n", 4);
  }
}

void EncoderImpl::encodeBigNumber(const std::string& value, Buffer::Instance& out) {
  out.add("(", 1);
  out.add(value);
  out.add("\r\n", 2);
}

void EncoderImpl::encodeBlobError(const std::string& error, Buffer::Instance& out) {
  encodeLengthPrefixed('!', error, out);
}

void EncoderImpl::encodeVerbatimString(const std::string& string, Buffer::Instance& out) {
  encodeLengthPrefixed('=', string, out);
}

void EncoderImpl::encodeMap(absl::Span<const RespValue> array, Buffer::Instance& out) {
  // The even-length invariant is enforced by the caller (encode()'s Map case), so ``array`` is
  // already a flat 2N k/v view; wire format count is N pairs.
  ASSERT(array.size() % 2 == 0);
  char buffer[32];
  char* current = buffer;
  *current++ = '%';
  current += StringUtil::itoa(current, 21, array.size() / 2);
  *current++ = '\r';
  *current++ = '\n';
  out.add(buffer, current - buffer);

  for (const RespValue& value : array) {
    encode(value, out);
  }
}

void EncoderImpl::encodeSet(absl::Span<const RespValue> array, Buffer::Instance& out) {
  encodeAggregate('~', array, out);
}

void EncoderImpl::encodePush(absl::Span<const RespValue> array, Buffer::Instance& out) {
  encodeAggregate('>', array, out);
}

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
