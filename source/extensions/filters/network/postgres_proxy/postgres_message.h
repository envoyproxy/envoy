#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

class String {
  std::string value_;

public:
  bool read(Buffer::Instance& data, uint64_t& pos) {
    // First find the terminating zero.
    char zero = 0;
    auto index = data.search(&zero, 1, pos);
    if (index == -1) {
      return false;
    }

    // reserve that much in the string
    value_.resize(index - pos);
    // Now copy from buffer to string
    data.copyOut(pos, index - pos, value_.data());
    pos += ((index - pos) + 1);

    return true;
  }

  std::string to_string() const { return "[" + value_ + "]"; }
};

template <typename T> class Int {
  T value_;

public:
  bool read(Buffer::Instance& data, uint64_t& pos) {
    value_ = data.peekBEInt<T>(pos);
    pos += sizeof(T);
    return true;
  }

  std::string to_string() const {
    char buf[32];
    sprintf(buf, "[%02d]", value_);
    return std::string(buf);
  }
};

using Int32 = Int<uint32_t>;
using Int16 = Int<uint16_t>;
using Int8 = Int<uint8_t>;

class Byte1 {
  char value_;

public:
  bool read(Buffer::Instance& data, uint64_t& pos) {
    value_ = data.peekBEInt<uint8_t>(pos);
    pos += sizeof(uint8_t);
    return true;
  }

  std::string to_string() const {
    char buf[4];
    sprintf(buf, "[%c]", value_);
    return std::string(buf);
  }
};

// This type is used as the last type ion the message and contains
// sequence of bytes.
class ByteN {
  std::vector<char> value_;

public:
  bool read(Buffer::Instance& data, uint64_t& pos) {
    // TODO do not use data length. but message length
    value_.resize(data.length() - pos);
    data.copyOut(pos, data.length() - pos, value_.data());
    pos += data.length();
    return true;
  }

  std::string to_string() const {
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
      out += buf;
    }
    out += "]";
    // return std::string(buf);
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
  std::vector<char> value_;

public:
  bool read(Buffer::Instance& data, uint64_t& pos) {
    int32_t len = data.peekBEInt<int32_t>(pos);
    pos += sizeof(int32_t);
    if (len < 1) {
      // nothing follows
      return true;
    }
    value_.resize(len);
    data.copyOut(pos, len, value_.data());
    pos += len;
    return true;
  }

  std::string to_string() const {
    char buf[16];
    sprintf(buf, "[%zu]", value_.size());

    std::string out = buf;
    out += "[";
    out.reserve(value_.size() * 3 + 16);
    bool first = true;
    for (const auto& i : value_) {
      if (first) {
        sprintf(buf, "%02d", i);
        first = false;
      } else {
        sprintf(buf, " %02d", i);
      }
      out += buf;
    }
    return out;
  }
};

template <typename T> class Array {
  std::vector<std::unique_ptr<T>> value_;

public:
  bool read(Buffer::Instance& data, uint64_t& pos) {
    // First read the 16 bits value which indicates how many
    // elements there are in the array.
    uint16_t num = data.peekBEInt<uint16_t>(pos);
    pos += sizeof(uint16_t);
    if (num != 0) {
      for (auto i = 0; i < num; i++) {
        auto item = std::make_unique<T>();
        item->read(data, pos);
        value_.push_back(std::move(item));
      }
    }
    return true;
  }
  std::string to_string() const {
    std::string out;
    char buf[128];
    sprintf(buf, "[Array of %zu:{", value_.size());
    out = buf;

    for (const auto& i : value_) {
      out += "[";
      out += i->to_string();
      out += "]";
    }
    out += "}]";

    return out;
  }
};

// Interface to Postgres message class.
class MessageI {
public:
  virtual ~MessageI() {}
  virtual void read(Buffer::Instance& data) = 0;
  virtual std::string to_string() const = 0;
};

template <typename... Types> class Sequence;

template <typename FirstField, typename... Remaining>
class Sequence<FirstField, Remaining...> : public MessageI {
  FirstField first_;
  Sequence<Remaining...> remaining_;

public:
  Sequence() {}
  std::string to_string() const override { return first_.to_string() + remaining_.to_string(); }

  void read(Buffer::Instance& data) override {
    uint64_t pos = 0;
    read(data, pos);
    // TODO return number of bytes read from data.
    // return pos
  }

  void read(Buffer::Instance& data, uint64_t& pos) {
    first_.read(data, pos);
    if (data.length() > 0) {
      remaining_.read(data, pos);
    }
  }
};

template <> class Sequence<> : public MessageI {
public:
  Sequence<>() {}
  std::string to_string() const override { return ""; }
  void read(Buffer::Instance&, uint64_t&) {}
  void read(Buffer::Instance&) override {}
};

template <typename... Types> std::unique_ptr<MessageI> createMsg() {
  return std::make_unique<Sequence<Types...>>();
}

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
