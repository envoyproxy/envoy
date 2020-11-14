#include "extensions/filters/network/common/redis/codec_impl.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <iostream>

#include "envoy/common/platform.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"

#include "absl/container/fixed_array.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

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
  std::cout << "----- RespType " <<  int(type_) << " | " << string_ << "\n";
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

RespValue::CompositeArray& RespValue::asCompositeArray() {
  ASSERT(type_ == RespType::CompositeArray);
  return composite_array_;
}

const RespValue::CompositeArray& RespValue::asCompositeArray() const {
  ASSERT(type_ == RespType::CompositeArray);
  return composite_array_;
}

void RespValue::cleanup() {
  // Need to manually delete because of the union.
  switch (type_) {
  case RespType::Array: {
    array_.~vector<RespValue>();
    break;
  }
  case RespType::CompositeArray: {
    composite_array_.~CompositeArray();
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
  noreply_ = false;
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
  case RespType::CompositeArray: {
    new (&composite_array_) CompositeArray();
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

RespValue::RespValue(const RespValue& other) : type_(RespType::Null) {
  type(other.type());
  switch (type_) {
  case RespType::Array: {
    asArray() = other.asArray();
    break;
  }
  case RespType::CompositeArray: {
    asCompositeArray() = other.asCompositeArray();
    break;
  }
  case RespType::SimpleString:
  case RespType::BulkString:
  case RespType::Error: {
    asString() = other.asString();
    break;
  }
  case RespType::Integer: {
    asInteger() = other.asInteger();
    break;
  }
  case RespType::Null:
    break;
  }
  noreply_ = other.noreply_;
}

RespValue::RespValue(RespValue&& other) noexcept : type_(other.type_) {
  switch (type_) {
  case RespType::Array: {
    new (&array_) std::vector<RespValue>(std::move(other.array_));
    break;
  }
  case RespType::CompositeArray: {
    new (&composite_array_) CompositeArray(std::move(other.composite_array_));
    break;
  }
  case RespType::SimpleString:
  case RespType::BulkString:
  case RespType::Error: {
    new (&string_) std::string(std::move(other.string_));
    break;
  }
  case RespType::Integer: {
    integer_ = other.integer_;
    break;
  }
  case RespType::Null:
    break;
  }
  noreply_ = other.noreply_;
}

RespValue& RespValue::operator=(const RespValue& other) {
  if (&other == this) {
    return *this;
  }
  type(other.type());
  switch (type_) {
  case RespType::Array: {
    asArray() = other.asArray();
    break;
  }
  case RespType::CompositeArray: {
    asCompositeArray() = other.asCompositeArray();
    break;
  }
  case RespType::SimpleString:
  case RespType::BulkString:
  case RespType::Error: {
    asString() = other.asString();
    break;
  }
  case RespType::Integer: {
    asInteger() = other.asInteger();
    break;
  }
  case RespType::Null:
    break;
  }
  noreply_ = other.noreply_;
  return *this;
}

RespValue& RespValue::operator=(RespValue&& other) noexcept {
  if (&other == this) {
    return *this;
  }

  type(other.type());
  switch (type_) {
  case RespType::Array: {
    array_ = std::move(other.array_);
    break;
  }
  case RespType::CompositeArray: {
    composite_array_ = std::move(other.composite_array_);
    break;
  }
  case RespType::SimpleString:
  case RespType::BulkString:
  case RespType::Error: {
    string_ = std::move(other.string_);
    break;
  }
  case RespType::Integer: {
    integer_ = other.integer_;
    break;
  }
  case RespType::Null:
    break;
  }
  noreply_ = other.noreply_;
  return *this;
}

bool RespValue::operator==(const RespValue& other) const {
  bool result = false;
  if (type_ != other.type()) {
    return result;
  }

  switch (type_) {
  case RespType::Array: {
    result = (asArray() == other.asArray());
    break;
  }
  case RespType::CompositeArray: {
    result = (asCompositeArray() == other.asCompositeArray());
    break;
  }
  case RespType::SimpleString:
  case RespType::BulkString:
  case RespType::Error: {
    result = (asString() == other.asString());
    break;
  }
  case RespType::Integer: {
    result = (asInteger() == other.asInteger());
    break;
  }
  case RespType::Null: {
    result = true;
    break;
  }
  }

  if (noreply_ != other.noreply_) {
    result = false;
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

void DecoderImpl::decode(Buffer::Instance &data) {
  for (const Buffer::RawSlice &slice : data.getRawSlices()) {
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
      default: {
        throw ProtocolError("invalid value type");
      }
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
  current += StringUtil::itoa(current, 21, array.size());
  *current++ = '\r';
  *current++ = '\n';
  out.add(buffer, current - buffer);

  for (const RespValue& value : array) {
    encode(value, out);
  }
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

void EncoderImpl::encodeBulkString(const std::string& string, Buffer::Instance& out) {
  char buffer[32];
  char* current = buffer;
  *current++ = '$';
  current += StringUtil::itoa(current, 21, string.size());
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

void MemcachedDecoder::parseSlice(const Buffer::RawSlice& slice) {
  const char* buffer = reinterpret_cast<const char*>(slice.mem_);
  uint64_t remaining = slice.len_;
  char* token = const_cast<char *>(buffer);

  while(remaining) {
    char ch = buffer[0];

    switch (state_) {
      case State::MC_START:
        if (ch == ' ') {
          break;
        } else if (ch == 'V' || islower(ch)) {
          state_ = State::MC_CHECK_CMD;
        } else if (isdigit(ch) || isupper(ch)) {
          state_ = State::MC_SIMPLE_STR;
        } else {
          throw ProtocolError("invalid character");
        }
        token = const_cast<char *>(buffer);

        pending_value_root_ = std::make_unique<RespValue>();
        pending_value_root_.get()->type(RespType::Array);

        break;
      case State::MC_SIMPLE_STR:
        if (ch == '\r') {
          std::string str(token, buffer-token);
          ENVOY_LOG(trace, "parse slice: simple string {}", str);
          pending_value_root_.get()->asArray().push_back(str);
          state_ = State::MC_DONE;
        }
        break;
      case State::MC_CHECK_CMD:
        if (ch == ' ' || ch == '\r') {
          cmd_ = std::string(token, buffer-token);

          pending_value_root_.get()->asArray().push_back(cmd_);
          if (cmd_ == "get" || cmd_ == "touch") {
            pending_value_root_.get()->asArray().back().asString().append("mc");
          }

          ENVOY_LOG(trace, "parse slice: check_cmd {}", cmd_);
          if (cmd_ == "get" || cmd_ == "gets") {
            state_ = State::MC_MULTI_KEY;
          } else if (cmd_ == "gat" || cmd_ == "gats") {
            state_ = State::MC_GAT_EXPIRE_BEGIN;
          } else if (cmd_ == "quit") {
            state_ = State::MC_DONE;
          } else {
            state_ = State::MC_SINGLE_KEY;
          }
          token = const_cast<char *>(buffer);
        }
        break;
      case State::MC_SINGLE_KEY:
        if (ch == '\r') {
          state_ = State::MC_DONE;
        } else if (ch == ' ') {
          token = const_cast<char *>(buffer);
        } else {
          state_ = State::MC_SINGLE_KEY_END;
        }
        break;
      case State::MC_SINGLE_KEY_END:
        if (ch == ' ' || ch == '\r') {
          std::string key(token+1, buffer-token-1);
          pending_value_root_.get()->asArray().push_back(key);

          if (ch == '\r') {
            state_ = State::MC_DONE;
            break;
          }
          if (cmd_ == "delete" || cmd_ == "incr" || cmd_ == "decr" || cmd_ == "touch") {
            state_ = State::MC_SINGLE_KEY_EXTRA;
          } else {
            state_ = State::MC_FLAGS;
          }
          token = const_cast<char *>(buffer);
        }
        break;
      case State::MC_SINGLE_KEY_EXTRA:
        if (ch == '\r') {
          std::string extra(token+1, buffer-token-1);
          ENVOY_LOG(trace, "single_key_extra: \"{}\"", extra);

          if (extra.size() >= 7 && extra.compare(extra.size()-7, 7, "noreply") == 0) {
            pending_value_root_.get()->noreply_ = true;

            extra.erase(extra.size()-7);
            if (extra.size() > 0) {
              std::size_t found = extra.find_last_not_of(" \t");
              ENVOY_LOG(trace, "data_key_extra noreply: {}, found: {}", extra, found);
              if (found != std::string::npos) {
                extra.erase(found+1);
              }
              ENVOY_LOG(trace, "data_key_extra noreply: {}, found: {}", extra, found);
            }
          }

          if (extra.size() > 0) {
            RespValuePtr value = std::make_unique<RespValue>();
            value.get()->type(RespType::BulkString);
            value.get()->asString().append(extra);
            pending_value_root_.get()->asArray().push_back(*value.get());
          }

          state_ = State::MC_DONE;
        }
        break;
      case State::MC_FLAGS:
        if (ch == ' ') {
            token = const_cast<char *>(buffer);
        } else {
            state_ = State::MC_FLAGS_END;
        }
        break;
      case State::MC_FLAGS_END:
        if (ch == ' ') {
          std::string flags(token+1, buffer-token-1);
          pending_value_root_.get()->asArray().push_back(flags);
          if (cmd_ == "VALUE") {
            state_ = State::MC_BYTES;
          } else {
            state_ = State::MC_EXPTIME;
          }
          token = const_cast<char *>(buffer);
        }
        break;
      case State::MC_EXPTIME:
        if (ch == ' ') {
            token = const_cast<char *>(buffer);
        } else {
            state_ = State::MC_EXPTIME_END;
        }
        break;
      case State::MC_EXPTIME_END:
        if (ch == ' ') {
          std::string exptime(token+1, buffer-token-1);
          pending_value_root_.get()->asArray().push_back(exptime);
          token = const_cast<char *>(buffer);
          state_ = State::MC_BYTES;
        }
        break;
      case State::MC_BYTES:
        if (ch == ' ') {
            token = const_cast<char *>(buffer);
            break;
        }
        state_ = State::MC_BYTES_END;
        pending_data_length_ = 0;
        // fallthrough
      case State::MC_BYTES_END:
        if (isdigit(ch)) {
            pending_data_length_ = pending_data_length_ * 10 + (ch - '0');
            ENVOY_LOG(trace, "+++++ update pending_data_length_ {}", pending_data_length_);
        } else if (ch == ' ' || ch == '\r') {
            std::string bytes(token+1, buffer-token-1);
            pending_value_root_.get()->asArray().push_back(bytes);
            state_ = State::MC_DATA_KEY_EXTRA;
            token = const_cast<char *>(buffer);
        } else {
            throw ProtocolError("invalid number " + std::string(token, buffer-token));
        }
        break;
      case State::MC_DATA_KEY_EXTRA:
        if (ch == '\n') {
          // FIXME(lvht)
          if (buffer-token-2 > 0) {
            std::string extra(token+1, buffer-token-2);

            ENVOY_LOG(trace, "data_key_extra: {}", extra);

            if (extra.size() >= 7 && extra.compare(extra.size()-7, 7, "noreply") == 0) {
              pending_value_root_.get()->noreply_ = true;

              extra.erase(extra.size()-7);
              if (extra.size() > 0) {
                std::size_t found = extra.find_last_not_of(" \t");
                ENVOY_LOG(trace, "data_key_extra noreply: {}, found: {}", extra, found);
                if (found != std::string::npos) {
                  extra.erase(found+1);
                }
                ENVOY_LOG(trace, "data_key_extra noreply: {}, found: {}", extra, found);
              }
            }

            if (extra.size() > 0) {
              pending_value_root_.get()->asArray().push_back(extra);
            }
          }

          // to store payload data
          pending_value_root_.get()->asArray().push_back(std::string(""));

          state_ = State::MC_DATA;
        }
        break;
      case State::MC_DATA: {
        ASSERT(pending_data_length_ >= 0);
        uint64_t length_to_copy =
            std::min(static_cast<uint64_t>(pending_data_length_), remaining);
        ENVOY_LOG(trace, "length_to_copy {} pending_data_length_ {}, buffer {}", length_to_copy, pending_data_length_, std::string(buffer, length_to_copy));
        pending_value_root_.get()->asArray().back().asString().append(buffer, length_to_copy);
        pending_data_length_ -= length_to_copy;
        remaining -= length_to_copy;
        buffer += length_to_copy;

        if (pending_data_length_ == 0) {
            ENVOY_LOG(trace, "parse slice: BulkStringBody complete: {}",
                pending_value_root_.get()->asArray().back().asString());
            if (cmd_ == "VALUE") {
              state_ = State::MC_DATA_SKIP_END;
            } else {
              state_ = State::MC_DATA_END;
            }
        }
        continue;
      }
      case State::MC_DATA_SKIP_END:
        if (ch == 'D') { // skip \r\nEND
            state_ = State::MC_DATA_END;
        }
        break;
      case State::MC_DATA_END:
        if (ch != '\r') {
            throw ProtocolError("need to read \\r");
        }
        state_ = State::MC_DONE;
        break;
      case State::MC_GAT_EXPIRE_BEGIN:
        if (isdigit(ch)) {
          token = const_cast<char *>(buffer);
          state_ = State::MC_GAT_EXPIRE_END;
        }
        break;
      case State::MC_GAT_EXPIRE_END:
        if (ch == ' ') {
          pending_value_root_.get()->asArray().push_back(std::string(token, buffer-token));
          state_ = State::MC_MULTI_KEY;
          token = const_cast<char *>(buffer);
        }
        break;
      case State::MC_MULTI_KEY:
        if (ch == ' ') {
          token = const_cast<char *>(buffer);
        } else {
          state_ = State::MC_MULTI_KEY_ONE;
          ENVOY_LOG(trace, "multi key one: {}", ch);
        }
        break;
      case State::MC_MULTI_KEY_ONE:
        ENVOY_LOG(trace, "multi key one-: {}", ch);
        if (ch == ' ' || ch == '\r') {
          ENVOY_LOG(trace, "append {}", buffer-token);
          std::string key(token+1, buffer-token-1);
          ENVOY_LOG(trace, "parse slice: one key {}", key);
          pending_value_root_.get()->asArray().push_back(key);

          if (ch == '\r') {
            state_ = State::MC_DONE;
          } else {
            state_ = State::MC_MULTI_KEY;
            token = const_cast<char *>(buffer);
          }
        }
        break;
      case State::MC_DONE:
        if (ch != '\n') {
            throw ProtocolError("need to read \\n");
        }
        state_ = State::MC_START;
        callbacks_.onRespValue(std::move(pending_value_root_));
        break;
      default:
        throw ProtocolError("unknown state");
    }

    buffer++;
    remaining--;
  }
}

void MemcachedDecoder::decode(Buffer::Instance &data) {
  for (const Buffer::RawSlice &slice : data.getRawSlices()) {
    parseSlice(slice);
  }

  data.drain(data.length());
}

void MemcachedEncoder::encode(const RespValue& value, Buffer::Instance& out) {
  if (value.type() == RespType::Error) {
    if (value.asString() == "noreply") {
    } else {
      out.add("SERVER_ERROR ");
      out.add(value.asString());
      out.add("\r\n");
    }
    return;
  }

  ASSERT(value.type() == RespType::Array);
  auto array = value.asArray();
  ASSERT(array.size() > 0);
  if (array.front().type() == RespType::BulkString) {
    auto cmd = array.front().asString();
    if (cmd == "touchmc") {
      cmd = "touch";
    } else if (cmd == "getmc") {
      cmd = "get";
    }
    out.add(cmd);

    if (cmd == "cas" || cmd == "VALUE"
        || cmd == "set" || cmd == "add" || cmd == "replace"
        || cmd == "prepend" || cmd == "append") {
      for (unsigned long i = 1; i < array.size()-1; i++) {
        out.add(" ", 1);
        out.add(array[i].asString());
      }
      out.add("\r\n", 2);
      out.add(array.back().asString());
    } else {
      for (unsigned long i = 1; i < array.size(); i++) {
        out.add(" ", 1);
        out.add(array[i].asString());
      }
    }
    out.add("\r\n", 2);
  } else {
    for (auto item : array) {
      ASSERT(item.type() == RespType::Array);
      // skip END response
      if (item.asArray()[0].asString() == "VALUE") {
        encode(item, out);
      }
    }
    out.add("END\r\n", 5);
  }
}
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
