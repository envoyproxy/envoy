#include "common/buffer/buffer_impl.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "fmt/printf.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

/**
 * Postgres messages are described in official Postgres documentation:
 * https://www.postgresql.org/docs/12/protocol-message-formats.html
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
    if ((data.length() - pos) < sizeof(T)) {
      return false;
    }
    value_ = data.peekBEInt<T>(pos);
    pos += sizeof(T);
    left -= sizeof(T);
    return true;
  }

  std::string toString() const { return fmt::format("[{}]", value_); }

  T get() const { return value_; }

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

private:
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
    // First read the 16 bits value which indicates how many
    // elements there are in the array.
    if (((data.length() - pos) < sizeof(uint16_t)) || (left < sizeof(uint16_t))) {
      return false;
    }
    const uint16_t num = data.peekBEInt<uint16_t>(pos);
    pos += sizeof(uint16_t);
    left -= sizeof(uint16_t);
    if (num != 0) {
      for (uint16_t i = 0; i < num; i++) {
        auto item = std::make_unique<T>();
        if (!item->read(data, pos, left)) {
          return false;
        }
        value_.push_back(std::move(item));
      }
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

private:
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
    if ((data.length() - pos) < left) {
      return false;
    }
    // Read until nothing is left.
    while (left != 0) {
      auto item = std::make_unique<T>();
      if (!item->read(data, pos, left)) {
        return false;
      }
      value_.push_back(std::move(item));
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

private:
  std::vector<std::unique_ptr<T>> value_;
};

// Interface to Postgres message class.
class Message {
public:
  virtual ~Message() = default;

  // read method should read only as many bytes from data
  // buffer as it is indicated in message's length field.
  // "length" parameter indicates how many bytes were indicated in Postgres message's
  // length field. "data" buffer may contain more bytes than "length".
  virtual bool read(const Buffer::Instance& data, const uint64_t length) PURE;

  // toString method provides displayable representation of
  // the Postgres message.
  virtual std::string toString() const PURE;
};

// Sequence is tuple like structure, which binds together
// set of several fields of different types.
template <typename... Types> class Sequence;

template <typename FirstField, typename... Remaining>
class Sequence<FirstField, Remaining...> : public Message {
  FirstField first_;
  Sequence<Remaining...> remaining_;

public:
  Sequence() = default;
  std::string toString() const override {
    return absl::StrCat(first_.toString(), remaining_.toString());
  }

  bool read(const Buffer::Instance& data, const uint64_t length) override {
    uint64_t pos = 0;
    uint64_t left = length;
    return read(data, pos, left);
  }

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
};

// Terminal template definition for variadic Sequence template.
template <> class Sequence<> : public Message {
public:
  Sequence<>() = default;
  std::string toString() const override { return ""; }
  bool read(const Buffer::Instance&, uint64_t&, uint64_t&) { return true; }
  bool read(const Buffer::Instance&, const uint64_t) override { return true; }
};

// Helper function to create pointer to a Sequence structure and is used by Postgres
// decoder after learning the type of Postgres message.
template <typename... Types> std::unique_ptr<Message> createMsgBodyReader() {
  return std::make_unique<Sequence<Types...>>();
}

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
