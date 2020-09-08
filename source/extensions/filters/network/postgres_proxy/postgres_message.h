#include "common/buffer/buffer_impl.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

/*
 * Postgres messages are described in official postgres documentation:
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
 * read - to read number bytes from received buffer. The number of bytes depends on structure type.
 * toString - method returns displayable representation of the structure value.
 *
 */

// Template for integer types.
// Size of integer types if fixed and known and depends on the type of integer.
template <typename T> class Int {
  T value_;

public:
  bool read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left) {
    if ((data.length() - pos) < sizeof(T)) {
      return false;
    }
    value_ = data.peekBEInt<T>(pos);
    pos += sizeof(T);
    left -= sizeof(T);
    return true;
  }

  std::string toString() const {
    char buf[32];
    sprintf(buf, getFormat(), value_);
    return std::string(buf);
  }

  constexpr const char* getFormat() const { return "[%02d]"; }
  T get() const { return value_; }
};

using Int32 = Int<uint32_t>;
using Int16 = Int<uint16_t>;
using Int8 = Int<uint8_t>;

// 8-bits character value
using Byte1 = Int<char>;

// getFormat method specialization for Byte1.
template <> constexpr const char* Int<char>::getFormat() const { return "[%c]"; }

// String type requires byte with zero value to indicate end of string.
class String {
  std::string value_;

public:
  bool read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left) {
    // First find the terminating zero.
    char zero = 0;
    auto index = data.search(&zero, 1, pos);
    if (index == -1) {
      return false;
    }

    // reserve that much in the string
    auto size = index - pos;
    value_.resize(size);
    // Now copy from buffer to string
    data.copyOut(pos, index - pos, value_.data());
    pos += (size + 1);
    left -= (size + 1);

    return true;
  }

  std::string toString() const { return absl::StrCat("[", value_, "]"); }
};

// ByteN type is used as the last type in the Postgres message and contains
// sequence of bytes. The length must be deduced from buffer length.
class ByteN {
  std::vector<uint8_t> value_;

public:
  bool read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left) {
    if (left > data.length()) {
      return false;
    }
    value_.resize(left);
    data.copyOut(pos, left, value_.data());
    pos += left;
    left = 0;
    return true;
  }

  std::string toString() const {
    std::string out = "[";
    out.reserve(value_.size() * 3);
    bool first = true;
    for (const auto& i : value_) {
      char buf[4];
      if (first) {
        sprintf(buf, "%02d", i);
        first = false;
      } else {
        sprintf(buf, " %02d", i);
      }
      absl::StrAppend(&out, buf);
    }
    absl::StrAppend(&out, "]");
    return out;
  }
};

// Class represents the structure consisting of 4 bytes of length
// indicating how many bytes follow.
// In Postgres documentation it is described as:
// Int32
// The length of the function result value, in bytes (this count does not include itself). Can be
// zero. As a special case, -1 indicates a NULL function result. No value bytes follow in the NULL
// case.
//
// ByteN
// The value of the function result, in the format indicated by the associated format code. n is the
// above length.
class VarByteN {
  int32_t len_;
  std::vector<char> value_;

public:
  bool read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left) {
    if ((left < sizeof(int32_t)) || ((data.length() - pos) < sizeof(int32_t))) {
      return false;
    }
    len_ = data.peekBEInt<int32_t>(pos);
    pos += sizeof(int32_t);
    left -= sizeof(int32_t);
    if (len_ < 1) {
      // nothing follows
      value_.clear();
      return true;
    }
    if ((left < static_cast<uint64_t>(len_)) ||
        ((data.length() - pos) < static_cast<uint64_t>(len_))) {
      return false;
    }
    value_.resize(len_);
    data.copyOut(pos, len_, value_.data());
    pos += len_;
    left -= len_;
    return true;
  }

  std::string toString() const {
    char buf[16];
    sprintf(buf, "[(%d bytes):", len_);

    std::string out = buf;
    out.reserve(value_.size() * 3 + 16);
    bool first = true;
    for (const auto& i : value_) {
      if (first) {
        sprintf(buf, "%02d", i);
        first = false;
      } else {
        sprintf(buf, " %02d", i);
      }
      absl::StrAppend(&out, buf);
    }
    absl::StrAppend(&out, "]");
    return out;
  }
};

// Array contains one or more values of the same type.
template <typename T> class Array {
  std::vector<std::unique_ptr<T>> value_;

public:
  bool read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left) {
    // First read the 16 bits value which indicates how many
    // elements there are in the array.
    if ((data.length() < sizeof(uint16_t)) || (left < sizeof(uint16_t))) {
      return false;
    }
    uint16_t num = data.peekBEInt<uint16_t>(pos);
    pos += sizeof(uint16_t);
    left -= sizeof(uint16_t);
    if (num != 0) {
      for (auto i = 0; i < num; i++) {
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
    std::string out;
    char buf[128];
    sprintf(buf, "[Array of %zu:{", value_.size());
    out = buf;

    for (const auto& i : value_) {
      absl::StrAppend(&out, i->toString());
    }
    absl::StrAppend(&out, "}]");

    return out;
  }
};

// Interface to Postgres message class.
class MessageI {
public:
  virtual ~MessageI() {}

  // read method should read only as many bytes from data
  // buffer as it is indicated in message's length field.
  // "length" parameter indicates how many bytes were indicated in Postgres message's
  // length field. "data" parameter may contain more bytes than "length".
  virtual bool read(const Buffer::Instance& data, const uint64_t length) = 0;

  // toString method provides displayable representation of
  // the Postgres message.
  virtual std::string toString() const = 0;
};

// Sequence is tuple like structure, which binds together
// set of several fields of different types.
template <typename... Types> class Sequence;

template <typename FirstField, typename... Remaining>
class Sequence<FirstField, Remaining...> : public MessageI {
  FirstField first_;
  Sequence<Remaining...> remaining_;

public:
  Sequence() {}
  std::string toString() const override {
    return absl::StrCat(first_.toString(), remaining_.toString());
  }

  bool read(const Buffer::Instance& data, const uint64_t length) override {
    uint64_t pos = 0;
    uint64_t left = length;
    return read(data, pos, left);
  }

  bool read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left) {
    auto result = first_.read(data, pos, left);
    if (!result) {
      return false;
    }
    return remaining_.read(data, pos, left);
  }
};

// Terminal template definition for variadic Sequence template.
template <> class Sequence<> : public MessageI {
public:
  Sequence<>() {}
  std::string toString() const override { return ""; }
  bool read(const Buffer::Instance&, uint64_t&, uint64_t&) { return true; }
  bool read(const Buffer::Instance&, const uint64_t) override { return true; }
};

// Helper function to create pointer to a Sequence structure and is used by Postgres
// decoder after learning the type of Postgres message.
template <typename... Types> std::unique_ptr<MessageI> createMsg() {
  return std::make_unique<Sequence<Types...>>();
}

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
