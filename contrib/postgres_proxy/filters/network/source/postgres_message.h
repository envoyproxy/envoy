#pragma once

#include "source/common/buffer/buffer_impl.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "fmt/printf.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

/**
 * Postgres messages are described in official Postgres documentation:
 * https://www.postgresql.org/docs/current/protocol-message-formats.html
 *
 * Most of messages start with 1-byte message identifier followed by 4-bytes length field. Few
 * messages are defined without starting 1-byte character and are used during well-defined initial
 * stage of connection process.
 *
 * Messages are composed from various fields: 8, 16, 32-bit integers, String, Arrays, etc.
 *
 * Structures defined below have the same naming as types used in official Postgres documentation.
 *
 * Each structure has the following methods:
 * read - to read number of bytes from received buffer. The number of bytes depends on structure
 * type. toString - method returns displayable representation of the structure value.
 *
 */

// Interface to Postgres message class.
class Message {
public:
  enum ValidationResult { ValidationFailed, ValidationOK, ValidationNeedMoreData };

  virtual ~Message() = default;

  // read method should read only as many bytes from data
  // buffer as it is indicated in message's length field.
  // "length" parameter indicates how many bytes were indicated in Postgres message's
  // length field. "data" buffer may contain more bytes than "length".
  virtual bool read(const Buffer::Instance& data, const uint64_t length) PURE;

  virtual ValidationResult validate(const Buffer::Instance& data, const uint64_t,
                                    const uint64_t) PURE;

  // toString method provides displayable representation of
  // the Postgres message.
  virtual std::string toString() const PURE;

protected:
  ValidationResult validation_result_{ValidationNeedMoreData};
};

// Template for integer types.
// Size of integer types is fixed and depends on the type of integer.
template <typename T> class Int {
public:
  /**
   * Read integer value from data buffer.
   * @param data reference to a buffer containing data to read.
   * @param pos offset in the buffer where data to read is located. Successful read will advance
   * this parameter.
   * @param left number of bytes to be read to reach the end of Postgres message.
   * Successful read will adjust this parameter.
   * @return boolean value indicating whether read was successful. If read returns
   * false "pos" and "left" params are not updated. When read is not successful,
   * the caller should not continue reading next values from the data buffer
   * for the current message.
   */
  bool read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left) {
    value_ = data.peekBEInt<T>(pos);
    pos += sizeof(T);
    left -= sizeof(T);
    return true;
  }

  Message::ValidationResult validate(const Buffer::Instance& data, const uint64_t, uint64_t& pos,
                                     uint64_t& left) {
    if (left < sizeof(T)) {
      return Message::ValidationFailed;
    }

    if ((data.length() - pos) < sizeof(T)) {
      return Message::ValidationNeedMoreData;
    }

    pos += sizeof(T);
    left -= sizeof(T);
    return Message::ValidationOK;
  }

  std::string toString() const { return fmt::format("[{}]", value_); }

  T get() const { return value_; }

  T& operator=(const T& other) {
    value_ = other;
    return value_;
  }

private:
  T value_{};
};

using Int32 = Int<uint32_t>;
using Int16 = Int<uint16_t>;
using Int8 = Int<uint8_t>;

// 8-bits character value.
using Byte1 = Int<char>;

// String type requires byte with zero value to indicate end of string.
class String {
public:
  /**
   * See above for parameter and return value description.
   */
  bool read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left);
  std::string toString() const;
  Message::ValidationResult validate(const Buffer::Instance&, const uint64_t start_offset,
                                     uint64_t&, uint64_t&);

private:
  // start_ and end_ are set by validate method.
  uint64_t start_;
  uint64_t end_;
  std::string value_;
};

// ByteN type is used as the last type in the Postgres message and contains
// sequence of bytes. The length must be deduced from message length.
class ByteN {
public:
  /**
   * See above for parameter and return value description.
   */
  bool read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left);
  std::string toString() const;
  Message::ValidationResult validate(const Buffer::Instance&, const uint64_t, uint64_t&, uint64_t&);

private:
  std::vector<uint8_t> value_;
};

// VarByteN represents the structure consisting of 4 bytes of length
// indicating how many bytes follow.
// In Postgres documentation it is described as:
// - Int32
//   The number of bytes in the structure (this count does not include itself). Can be
//   zero. As a special case, -1 indicates a NULL (no result). No value bytes follow in the NULL
// case.
//
// - ByteN
// The sequence of bytes representing the value. Bytes are present only when length has a positive
// value.
class VarByteN {
public:
  /**
   * See above for parameter and return value description.
   */
  bool read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left);
  std::string toString() const;
  Message::ValidationResult validate(const Buffer::Instance&, const uint64_t, uint64_t&, uint64_t&);

private:
  int32_t len_;
  std::vector<uint8_t> value_;
};

// Array contains one or more values of the same type.
template <typename T> class Array {
public:
  /**
   * See above for parameter and return value description.
   */
  bool read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left) {
    // Skip reading the size of array. The validator did it.
    pos += sizeof(uint16_t);
    left -= sizeof(uint16_t);
    for (uint16_t i = 0; i < size_; i++) {
      value_[i]->read(data, pos, left);
    }
    return true;
  }

  std::string toString() const {
    std::string out = fmt::format("[Array of {}:{{", value_.size());

    // Iterate through all elements in the array.
    // No delimiter is required between elements, as each
    // element is wrapped in "[]" or "{}".
    for (const auto& i : value_) {
      absl::StrAppend(&out, i->toString());
    }
    absl::StrAppend(&out, "}]");

    return out;
  }
  Message::ValidationResult validate(const Buffer::Instance& data, const uint64_t start_offset,
                                     uint64_t& pos, uint64_t& left) {
    // First read the 16 bits value which indicates how many
    // elements there are in the array.
    if (left < sizeof(uint16_t)) {
      return Message::ValidationFailed;
    }

    if ((data.length() - pos) < sizeof(uint16_t)) {
      return Message::ValidationNeedMoreData;
    }

    size_ = data.peekBEInt<uint16_t>(pos);
    uint64_t orig_pos = pos;
    uint64_t orig_left = left;
    pos += sizeof(uint16_t);
    left -= sizeof(uint16_t);
    if (size_ != 0) {
      for (uint16_t i = 0; i < size_; i++) {
        auto item = std::make_unique<T>();
        Message::ValidationResult result = item->validate(data, start_offset, pos, left);
        if (Message::ValidationOK != result) {
          pos = orig_pos;
          left = orig_left;
          value_.clear();
          return result;
        }
        value_.push_back(std::move(item));
      }
    }
    return Message::ValidationOK;
  }

private:
  uint16_t size_;
  std::vector<std::unique_ptr<T>> value_;
};

// Repeated is a composite type used at the end of the message.
// It indicates to read the value of the same type until the end
// of the Postgres message.
template <typename T> class Repeated {
public:
  /**
   * See above for parameter and return value description.
   */
  bool read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left) {
    for (size_t i = 0; i < value_.size(); i++) {
      if (!value_[i]->read(data, pos, left)) {
        return false;
      }
    }
    return true;
  }

  std::string toString() const {
    std::string out;

    // Iterate through all repeated elements.
    // No delimiter is required between elements, as each
    // element is wrapped in "[]" or "{}".
    for (const auto& i : value_) {
      absl::StrAppend(&out, i->toString());
    }
    return out;
  }
  Message::ValidationResult validate(const Buffer::Instance& data, const uint64_t start_offset,
                                     uint64_t& pos, uint64_t& left) {
    if ((data.length() - pos) < left) {
      return Message::ValidationNeedMoreData;
    }

    // Validate until the end of the message.
    uint64_t orig_pos = pos;
    uint64_t orig_left = left;
    while (left != 0) {
      auto item = std::make_unique<T>();
      Message::ValidationResult result = item->validate(data, start_offset, pos, left);
      if (Message::ValidationOK != result) {
        pos = orig_pos;
        left = orig_left;
        value_.clear();
        return result;
      }
      value_.push_back(std::move(item));
    }

    return Message::ValidationOK;
  }

private:
  std::vector<std::unique_ptr<T>> value_;
};

// ZeroTCodes (Zero Terminated Codes) is a structure which represents sequence of:
// [code][value]
// [code][value]
// ...
// [code][value]
// zero
// Where code is 1 Byte.
template <typename T> class ZeroTCodes {
public:
  bool read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left) {
    for (size_t i = 0; i < values_.size(); i++) {
      if (!values_[i].first.read(data, pos, left)) {
        return false;
      }
      if (!values_[i].second->read(data, pos, left)) {
        return false;
      }
    }
    return true;
  }

  std::string toString() const {
    std::string out;
    for (const auto& value : values_) {
      out += fmt::format("[{}:{}]", value.first.toString(), value.second->toString());
    }
    return out;
  }

  Message::ValidationResult validate(const Buffer::Instance& data, const uint64_t start_offset,
                                     uint64_t& pos, uint64_t& left) {
    uint64_t orig_pos = pos;
    uint64_t orig_left = left;
    // Check if one byte of code can be read.
    while (left >= sizeof(Byte1)) {
      // Read 1 byte code and exit when terminating zero is found.
      Byte1 code;
      code = data.peekBEInt<char>(pos);
      // Advance the position by 1 byte (code).
      pos += sizeof(Byte1);
      left -= sizeof(Byte1);
      if (code.get() == 0) {
        return Message::ValidationOK;
      }

      // Validate the value found after the code.
      auto item = std::make_unique<T>();
      Message::ValidationResult result = item->validate(data, start_offset, pos, left);
      if (Message::ValidationOK != result) {
        pos = orig_pos;
        left = orig_left;
        values_.clear();
        return result;
      }
      values_.push_back(std::make_pair(code, std::move(item)));
    }

    return Message::ValidationFailed;
  }

  std::vector<std::pair<Byte1, std::unique_ptr<T>>> values_;
};

// Sequence is tuple like structure, which binds together
// set of several fields of different types.
template <typename... Types> class Sequence;

template <typename FirstField, typename... Remaining> class Sequence<FirstField, Remaining...> {
  FirstField first_;
  Sequence<Remaining...> remaining_;

public:
  Sequence() = default;
  std::string toString() const { return absl::StrCat(first_.toString(), remaining_.toString()); }

  /**
   * Implementation of "read" method for variadic template.
   * It reads data for the current type and invokes read operation
   * for remaining types.
   * See above for parameter and return value description for individual types.
   */
  bool read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left) {
    bool result = first_.read(data, pos, left);
    if (!result) {
      return false;
    }
    return remaining_.read(data, pos, left);
  }

  Message::ValidationResult validate(const Buffer::Instance& data, const uint64_t start_offset,
                                     uint64_t& pos, uint64_t& left) {
    Message::ValidationResult result = first_.validate(data, start_offset, pos, left);
    if (result != Message::ValidationOK) {
      return result;
    }
    return remaining_.validate(data, start_offset, pos, left);
  }
};

// Terminal template definition for variadic Sequence template.
template <> class Sequence<> {
public:
  Sequence<>() = default;
  std::string toString() const { return ""; }
  bool read(const Buffer::Instance&, uint64_t&, uint64_t&) { return true; }
  Message::ValidationResult validate(const Buffer::Instance&, const uint64_t, uint64_t&,
                                     uint64_t&) {
    return Message::ValidationOK;
  }
};

template <typename... Types> class MessageImpl : public Message, public Sequence<Types...> {
public:
  ~MessageImpl() override = default;
  bool read(const Buffer::Instance& data, const uint64_t length) override {
    // Do not call read unless validation was successful.
    ASSERT(validation_result_ == ValidationOK);
    uint64_t pos = 0;
    uint64_t left = length;
    return Sequence<Types...>::read(data, pos, left);
  }
  Message::ValidationResult validate(const Buffer::Instance& data, const uint64_t start_pos,
                                     const uint64_t length) override {
    uint64_t pos = start_pos;
    uint64_t left = length;
    validation_result_ = Sequence<Types...>::validate(data, start_pos, pos, left);
    if (validation_result_ != Message::ValidationOK) {
      return validation_result_;
    }
    // verify that parser iterated over entire message and nothing is left.
    if (left != 0) {
      validation_result_ = Message::ValidationFailed;
    }
    return validation_result_;
  }
  std::string toString() const override { return Sequence<Types...>::toString(); }

private:
  // Message::ValidationResult validation_result_;
};

// Helper function to create pointer to a Sequence structure and is used by Postgres
// decoder after learning the type of Postgres message.
template <typename... Types> std::unique_ptr<Message> createMsgBodyReader() {
  return std::make_unique<MessageImpl<Types...>>();
}

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
