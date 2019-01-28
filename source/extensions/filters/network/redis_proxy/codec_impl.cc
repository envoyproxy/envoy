#include "extensions/filters/network/redis_proxy/codec_impl.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/stack_array.h"
#include "common/common/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

std::string RespValue::toString() const {
  switch (type_) {
  case RespType::Array: {
    std::string ret = "[";
    for (uint64_t i = 0; i < asArray().size(); i++) {
      ret += asArray()[i].toString();
      if (i != asArray().size() - 1) {
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
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

std::vector<RespValue>& RespValue::asArray() {
  ASSERT(type_ == RespType::Array);
  return array_;
}

const std::vector<RespValue>& RespValue::asArray() const {
  ASSERT(type_ == RespType::Array);
  return array_;
}

std::string& RespValue::asString() {
  ASSERT(type_ == RespType::BulkString || type_ == RespType::Error ||
         type_ == RespType::SimpleString);
  return string_;
}

const std::string& RespValue::asString() const {
  ASSERT(type_ == RespType::BulkString || type_ == RespType::Error ||
         type_ == RespType::SimpleString);
  return string_;
}

int64_t& RespValue::asInteger() {
  ASSERT(type_ == RespType::Integer);
  return integer_;
}

int64_t RespValue::asInteger() const {
  ASSERT(type_ == RespType::Integer);
  return integer_;
}

void RespValue::cleanup() {
  // Need to manually delete because of the union.
  switch (type_) {
  case RespType::Array: {
    array_.~vector<RespValue>();
    break;
  }
  case RespType::SimpleString:
  case RespType::BulkString:
  case RespType::Error: {
    string_.~basic_string<char>();
    break;
  }
  case RespType::Null:
  case RespType::Integer: {
    break;
  }
  }
}

void RespValue::type(RespType type) {
  cleanup();

  // Need to use placement new because of the union.
  type_ = type;
  switch (type) {
  case RespType::Array: {
    new (&array_) std::vector<RespValue>();
    break;
  }
  case RespType::SimpleString:
  case RespType::BulkString:
  case RespType::Error: {
    new (&string_) std::string();
    break;
  }
  case RespType::Null:
  case RespType::Integer: {
    break;
  }
  }
}

void DecoderImpl::decode(Buffer::Instance& data) {
  uint64_t num_slices = data.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
  data.getRawSlices(slices.begin(), num_slices);
  for (const Buffer::RawSlice& slice : slices) {
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
      pending_value_root_ = std::make_unique<RespValue>();
      pending_value_stack_.push_front({pending_value_root_.get(), 0});
      state_ = State::ValueStart;
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
      default: { throw ProtocolError("invalid value type"); }
      }

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
          pending_integer_.integer_ = (pending_integer_.integer_ * 10) + (c - '0');
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

      ENVOY_LOG(trace, "parse slice: IntegerLF: {}", pending_integer_.integer_);
      remaining--;
      buffer++;

      PendingValue& current_value = pending_value_stack_.front();
      if (current_value.value_->type() == RespType::Array) {
        if (pending_integer_.negative_) {
          // Null array. Convert to null.
          current_value.value_->type(RespType::Null);
          state_ = State::ValueComplete;
        } else if (pending_integer_.integer_ == 0) {
          state_ = State::ValueComplete;
        } else {
          std::vector<RespValue> values(pending_integer_.integer_);
          current_value.value_->asArray().swap(values);
          pending_value_stack_.push_front({&current_value.value_->asArray()[0], 0});
          state_ = State::ValueStart;
        }
      } else if (current_value.value_->type() == RespType::Integer) {
        if (pending_integer_.integer_ == 0 || !pending_integer_.negative_) {
          current_value.value_->asInteger() = pending_integer_.integer_;
        } else {
          // By subtracting 1 (and later correcting) we ensure that we remain within the int64_t
          // range to allow a valid static_cast. This is an issue when we have a value of -2^63,
          // which cannot be represented as 2^63 in the intermediate int64_t.
          current_value.value_->asInteger() =
              static_cast<int64_t>(pending_integer_.integer_ - 1) * -1 - 1;
        }
        state_ = State::ValueComplete;
      } else {
        ASSERT(current_value.value_->type() == RespType::BulkString);
        if (!pending_integer_.negative_) {
          // TODO(mattklein123): reserve and define max length since we don't stream currently.
          state_ = State::BulkStringBody;
        } else {
          // Null bulk string. Switch type to null and move to value complete.
          current_value.value_->type(RespType::Null);
          state_ = State::ValueComplete;
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
        state_ = State::LF;
      } else {
        pending_value_stack_.front().value_->asString().push_back(buffer[0]);
      }

      remaining--;
      buffer++;
      break;
    }

    case State::ValueComplete: {
      ENVOY_LOG(trace, "parse slice: ValueComplete");
      ASSERT(!pending_value_stack_.empty());
      pending_value_stack_.pop_front();
      if (pending_value_stack_.empty()) {
        callbacks_.onRespValue(std::move(pending_value_root_));
        state_ = State::ValueRootStart;
      } else {
        PendingValue& current_value = pending_value_stack_.front();
        ASSERT(current_value.value_->type() == RespType::Array);
        if (current_value.current_array_element_ < current_value.value_->asArray().size() - 1) {
          current_value.current_array_element_++;
          pending_value_stack_.push_front(
              {&current_value.value_->asArray()[current_value.current_array_element_], 0});
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
    out.add("$-1\r\n", 5);
    break;
  }
  case RespType::Integer:
    encodeInteger(value.asInteger(), out);
    break;
  }
}

void EncoderImpl::encodeArray(const std::vector<RespValue>& array, Buffer::Instance& out) {
  char buffer[32];
  char* current = buffer;
  *current++ = '*';
  current += StringUtil::itoa(current, 31, array.size());
  *current++ = '\r';
  *current++ = '\n';
  out.add(buffer, current - buffer);

  for (const RespValue& value : array) {
    encode(value, out);
  }
}

void EncoderImpl::encodeBulkString(const std::string& string, Buffer::Instance& out) {
  char buffer[32];
  char* current = buffer;
  *current++ = '$';
  current += StringUtil::itoa(current, 31, string.size());
  *current++ = '\r';
  *current++ = '\n';
  out.add(buffer, current - buffer);
  out.add(string);
  out.add("\r\n", 2);
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
    current += StringUtil::itoa(current, 31, integer);
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

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
