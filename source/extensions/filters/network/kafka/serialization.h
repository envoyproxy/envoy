#pragma once

#include "extensions/filters/network/kafka/kafka_types.h"

#include "common/common/fmt.h"
#include "common/common/byte_order.h"
#include "envoy/common/pure.h"
#include "envoy/common/exception.h"

#include <vector>
#include <string>
#include <memory>
#include <algorithm>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

// =============================================================================
// === DESERIALIZERS ===========================================================
// =============================================================================

/**
 * The general idea of Buffer is that it can be feed-ed data until it is ready
 * When true == ready(), it is safe to call get()
 * Further feed()-ing should have no effect on a buffer
 * (should return 0 and not move buffer/remaining)
 */

// === ABSTRACT DESERIALIZER ===================================================

template <typename T>
class Deserializer {
public:
  virtual size_t feed(const char*& buffer, uint64_t& remaining) PURE;
  virtual bool ready() const PURE;
  virtual T get() const PURE;
  virtual ~Deserializer() {};
};

// === INT BUFFERS =============================================================

template <typename T>
class IntBuffer : public Deserializer<T> {
public:
  IntBuffer(): written_{0}, ready_(false) {};

  size_t feed(const char*& buffer, uint64_t& remaining) {
    const size_t available = std::min<size_t>(sizeof(buf_) - written_, remaining);
    memcpy(buf_ + written_, buffer, available);
    written_ += available;

    if (written_ == sizeof(buf_)) {
      ready_ = true;
    }

    buffer += available;
    remaining -= available;

    return available;
  }

  bool ready() const {
    return ready_;
  }


protected:
  char buf_[sizeof(T) / sizeof(char)];
  size_t written_;
  bool ready_;
};

class Int8Buffer : public IntBuffer<INT8> {
public:
  INT8 get() const {
    return *reinterpret_cast<const INT8*>(buf_);
  }
};

class Int16Buffer : public IntBuffer<INT16> {
public:
  INT16 get() const {
    return be16toh(*reinterpret_cast<const INT16*>(buf_));
  }
};

class Int32Buffer : public IntBuffer<INT32> {
public:
  INT32 get() const {
    return be32toh(*reinterpret_cast<const INT32*>(buf_));
  }
};

class UInt32Buffer : public IntBuffer<UINT32> {
public:
  UINT32 get() const {
    return be32toh(*reinterpret_cast<const UINT32*>(buf_));
  }
};

class Int64Buffer : public IntBuffer<INT64> {
public:
  INT64 get() const {
    return be64toh(*reinterpret_cast<const INT64*>(buf_));
  }
};

// === BOOL BUFFER =============================================================

/**
 * Represents a boolean value in a byte.
 * Values 0 and 1 are used to represent false and true respectively.
 * When reading a boolean value, any non-zero value is considered true.
 */
class BoolBuffer : public Deserializer<BOOLEAN> {
public:
  BoolBuffer() {};

  size_t feed(const char*& buffer, uint64_t& remaining) {
    return buffer_.feed(buffer, remaining);
  }

  bool ready() const {
    return buffer_.ready();
  }

  BOOLEAN get() const {
    return 0 != buffer_.get();
  }


private:
  Int8Buffer buffer_;
};

// === STRING BUFFER ===========================================================

class StringBuffer : public Deserializer<STRING> {
public:

  size_t feed(const char*& buffer, uint64_t& remaining) {
    const size_t length_consumed = length_buf_.feed(buffer, remaining);
    if (!length_buf_.ready()) {
      // break early: we still need to fill in length buffer
      return length_consumed;
    }

    if (!length_consumed_) {
      required_ = length_buf_.get();
      if (required_ >= 0) {
        data_buf_ = std::vector<char>(required_);
      } else {
        throw EnvoyException(fmt::format("invalid STRING length: {}", required_));
      }
      length_consumed_ = true;
    }

    const size_t data_consumed = std::min<size_t>(required_, remaining);
    const size_t written = data_buf_.size() - required_;
    memcpy(data_buf_.data() + written, buffer, data_consumed);
    required_ -= data_consumed;

    buffer += data_consumed;
    remaining -= data_consumed;

    if (required_ == 0) {
      ready_ = true;
    }

    return length_consumed + data_consumed;
  }

  bool ready() const {
    return ready_;
  }

  STRING get() const {
    return std::string(data_buf_.begin(), data_buf_.end());
  }

private:
  Int16Buffer length_buf_;
  bool length_consumed_{false};

  INT16 required_;
  std::vector<char> data_buf_;

  bool ready_{false};
};

class NullableStringBuffer : public Deserializer<NULLABLE_STRING> {
public:
  size_t feed(const char*& buffer, uint64_t& remaining) {
    const size_t length_consumed = length_buf_.feed(buffer, remaining);
    if (!length_buf_.ready()) {
      // break early: we still need to fill in length buffer
      return length_consumed;
    }

    if (!length_consumed_) {
      required_ = length_buf_.get();

      if (required_ >= 0) {
        data_buf_ = std::vector<char>(required_);
      }
      if (required_ == NULL_STRING_LENGTH) {
        ready_ = true;
      }
      if (required_ < NULL_STRING_LENGTH) {
        throw EnvoyException(fmt::format("invalid NULLABLE_STRING length: {}", required_));
      }

      length_consumed_ = true;
    }

    if (ready_) {
      return length_consumed;
    }

    const size_t data_consumed = std::min<size_t>(required_, remaining);
    const size_t written = data_buf_.size() - required_;
    memcpy(data_buf_.data() + written, buffer, data_consumed);
    required_ -= data_consumed;

    buffer += data_consumed;
    remaining -= data_consumed;

    if (required_ == 0) {
      ready_ = true;
    }

    return length_consumed + data_consumed;
  }

  bool ready() const {
    return ready_;
  }

  NULLABLE_STRING get() const {
    return required_ >= 0 ? absl::make_optional(std::string(data_buf_.begin(), data_buf_.end())) : absl::nullopt;
  }

private:
  constexpr static INT16 NULL_STRING_LENGTH{-1};

  Int16Buffer length_buf_;
  bool length_consumed_{false};

  INT16 required_;
  std::vector<char> data_buf_;

  bool ready_{false};
};

// === BYTES BUFFERS ===========================================================

/**
 * Represents a raw sequence of bytes or null.
 * For non-null values, first the length N is given as an INT32. Then N bytes follow.
 * A null value is encoded with length of -1 and there are no following bytes.
 */

/**
 * This buffer ignores the data fed, the only result is the number of bytes ignored
 */
class NullableBytesIgnoringBuffer : public Deserializer<INT32> {
public:

  size_t feed(const char*& buffer, uint64_t& remaining) {
    const size_t length_consumed = length_buf_.feed(buffer, remaining);
    if (!length_buf_.ready()) {
      // break early: we still need to fill in length buffer
      return length_consumed;
    }

    if (!length_consumed_) {
      required_max_ = length_buf_.get();
      required_ = length_buf_.get();

      if (required_ == NULL_BYTES_LENGTH) {
        ready_ = true;
      }
      if (required_ < NULL_BYTES_LENGTH) {
        throw EnvoyException(fmt::format("invalid NULLABLE_BYTES length: {}", required_));
      }

      length_consumed_ = true;
    }

    if (ready_) {
      return length_consumed;
    }

    const size_t data_consumed = std::min<size_t>(required_, remaining);
    required_ -= data_consumed;

    buffer += data_consumed;
    remaining -= data_consumed;

    if (required_ == 0) {
      ready_ = true;
    }

    return length_consumed + data_consumed;
  }

  bool ready() const {
    return ready_;
  }

  /**
   * Returns length of ignored array, or -1 if that was null
   */
  INT32 get() const {
    return required_max_;
  }

private:
  constexpr static INT32 NULL_BYTES_LENGTH{-1};

  Int32Buffer length_buf_;
  bool length_consumed_{false};
  INT32 required_max_;
  INT32 required_;
  bool ready_{false};
};

/**
 * This buffer captures the data fed
 */
class NullableBytesCapturingBuffer : public Deserializer<NULLABLE_BYTES> {
public:

  size_t feed(const char*& buffer, uint64_t& remaining) {
    const size_t length_consumed = length_buf_.feed(buffer, remaining);
    if (!length_buf_.ready()) {
      // break early: we still need to fill in length buffer
      return length_consumed;
    }

    if (!length_consumed_) {
      required_ = length_buf_.get();

      if (required_ >= 0) {
        data_buf_ = std::vector<unsigned char>(required_);
      }
      if (required_ == NULL_BYTES_LENGTH) {
        ready_ = true;
      }
      if (required_ < NULL_BYTES_LENGTH) {
        throw EnvoyException(fmt::format("invalid NULLABLE_BYTES length: {}", required_));
      }

      length_consumed_ = true;
    }

    if (ready_) {
      return length_consumed;
    }

    const size_t data_consumed = std::min<size_t>(required_, remaining);
    const size_t written = data_buf_.size() - required_;
    memcpy(data_buf_.data() + written, buffer, data_consumed);
    required_ -= data_consumed;

    buffer += data_consumed;
    remaining -= data_consumed;

    if (required_ == 0) {
      ready_ = true;
    }

    return length_consumed + data_consumed;
  }

  bool ready() const {
    return ready_;
  }

  NULLABLE_BYTES get() const {
    if (NULL_BYTES_LENGTH == required_) {
      return absl::nullopt;
    } else {
      return { data_buf_ };
    }
  }

private:
  constexpr static INT32 NULL_BYTES_LENGTH{-1};

  Int32Buffer length_buf_;
  bool length_consumed_{false};
  INT32 required_;

  std::vector<unsigned char> data_buf_;
  bool ready_{false};
};

// === COMPOSITE BUFFER ========================================================

template <typename RT, typename...> class CompositeBuffer;

template <typename RT, typename T1>
class CompositeBuffer<RT, T1>: public Deserializer<RT> {
public:
  CompositeBuffer() {};
  size_t feed(const char*& buffer, uint64_t& remaining) {
    size_t consumed = 0;
    consumed += buffer1_.feed(buffer, remaining);
    return consumed;
  }
  bool ready() const {
    return buffer1_.ready();
  }
  RT get() const {
    return { buffer1_.get() };
  }
protected:
  T1 buffer1_;
};

template <typename RT, typename T1, typename T2>
class CompositeBuffer<RT, T1, T2>: public Deserializer<RT> {
public:
  CompositeBuffer() {};
  size_t feed(const char*& buffer, uint64_t& remaining) {
    size_t consumed = 0;
    consumed += buffer1_.feed(buffer, remaining);
    consumed += buffer2_.feed(buffer, remaining);
    return consumed;
  }
  bool ready() const {
    return buffer2_.ready();
  }
  RT get() const {
    return { buffer1_.get(), buffer2_.get() };
  }
protected:
  T1 buffer1_;
  T2 buffer2_;
};

template <typename RT, typename T1, typename T2, typename T3>
class CompositeBuffer<RT, T1, T2, T3>: public Deserializer<RT> {
public:
  CompositeBuffer() {};
  size_t feed(const char*& buffer, uint64_t& remaining) {
    size_t consumed = 0;
    consumed += buffer1_.feed(buffer, remaining);
    consumed += buffer2_.feed(buffer, remaining);
    consumed += buffer3_.feed(buffer, remaining);
    return consumed;
  }
  bool ready() const {
    return buffer3_.ready();
  }
  RT get() const {
    return { buffer1_.get(), buffer2_.get(), buffer3_.get() };
  }
protected:
  T1 buffer1_;
  T2 buffer2_;
  T3 buffer3_;
};

template <typename RT, typename T1, typename T2, typename T3, typename T4>
class CompositeBuffer<RT, T1, T2, T3, T4>: public Deserializer<RT> {
public:
  CompositeBuffer() {};
  size_t feed(const char*& buffer, uint64_t& remaining) {
    size_t consumed = 0;
    consumed += buffer1_.feed(buffer, remaining);
    consumed += buffer2_.feed(buffer, remaining);
    consumed += buffer3_.feed(buffer, remaining);
    consumed += buffer4_.feed(buffer, remaining);
    return consumed;
  }
  bool ready() const {
    return buffer4_.ready();
  }
  RT get() const {
    return { buffer1_.get(), buffer2_.get(), buffer3_.get(), buffer4_.get() };
  }
protected:
  T1 buffer1_;
  T2 buffer2_;
  T3 buffer3_;
  T4 buffer4_;
};

template <typename RT, typename T1, typename T2, typename T3, typename T4, typename T5>
class CompositeBuffer<RT, T1, T2, T3, T4, T5>: public Deserializer<RT> {
public:
  CompositeBuffer() {};
  size_t feed(const char*& buffer, uint64_t& remaining) {
    size_t consumed = 0;
    consumed += buffer1_.feed(buffer, remaining);
    consumed += buffer2_.feed(buffer, remaining);
    consumed += buffer3_.feed(buffer, remaining);
    consumed += buffer4_.feed(buffer, remaining);
    consumed += buffer5_.feed(buffer, remaining);
    return consumed;
  }
  bool ready() const {
    return buffer5_.ready();
  }
  RT get() const {
    return { buffer1_.get(), buffer2_.get(), buffer3_.get(), buffer4_.get(), buffer5_.get() };
  }
protected:
  T1 buffer1_;
  T2 buffer2_;
  T3 buffer3_;
  T4 buffer4_;
  T5 buffer5_;
};

template <typename RT, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
class CompositeBuffer<RT, T1, T2, T3, T4, T5, T6>: public Deserializer<RT> {
public:
  CompositeBuffer() {};
  size_t feed(const char*& buffer, uint64_t& remaining) {
    size_t consumed = 0;
    consumed += buffer1_.feed(buffer, remaining);
    consumed += buffer2_.feed(buffer, remaining);
    consumed += buffer3_.feed(buffer, remaining);
    consumed += buffer4_.feed(buffer, remaining);
    consumed += buffer5_.feed(buffer, remaining);
    consumed += buffer6_.feed(buffer, remaining);
    return consumed;
  }
  bool ready() const {
    return buffer6_.ready();
  }
  RT get() const {
    return { buffer1_.get(), buffer2_.get(), buffer3_.get(), buffer4_.get(), buffer5_.get(), buffer6_.get() };
  }
protected:
  T1 buffer1_;
  T2 buffer2_;
  T3 buffer3_;
  T4 buffer4_;
  T5 buffer5_;
  T6 buffer6_;
};

// === ARRAY BUFFER ============================================================

/**
 * Represents a sequence of objects of a given type T. Type T can be either a primitive type (e.g. STRING) or a structure.
 * First, the length N is given as an INT32. Then N instances of type T follow.
 * A null array is represented with a length of -1.
 */

template <typename RT, typename CT>
class ArrayBuffer : public Deserializer<NULLABLE_ARRAY<RT>> {
public:
  size_t feed(const char*& buffer, uint64_t& remaining) {

    const size_t length_consumed = length_buf_.feed(buffer, remaining);
    if (!length_buf_.ready()) {
      // break early: we still need to fill in length buffer
      return length_consumed;
    }

    if (!length_consumed_) {
      required_ = length_buf_.get();

      if (required_ >= 0) {
        children_ = std::vector<CT>(required_);
      }
      if (required_ == NULL_ARRAY_LENGTH) {
        ready_ = true;
      }
      if (required_ < NULL_ARRAY_LENGTH) {
        throw EnvoyException(fmt::format("invalid array length: {}", required_));
      }

      length_consumed_ = true;
    }

    if (ready_) {
      return length_consumed;
    }

    size_t child_consumed{0};
    for (CT& child : children_) {
      child_consumed += child.feed(buffer, remaining);
    }

    bool children_ready_ = true;
    for (CT& child : children_) {
      children_ready_ &= child.ready();
    }
    ready_ = children_ready_;

    return length_consumed + child_consumed;
  }

  bool ready() const {
    return ready_;
  }

  NULLABLE_ARRAY<RT> get() const {
    if (NULL_ARRAY_LENGTH != required_) {
      std::vector<RT> result{};
      result.reserve(children_.size());
      for (const CT& child : children_) {
        const RT child_result = child.get();
        result.push_back(child_result);
      }
      return { result };
    } else {
      return absl::nullopt;
    }
  }

private:
  constexpr static INT32 NULL_ARRAY_LENGTH{-1};

  Int32Buffer length_buf_;
  bool length_consumed_{false};
  INT32 required_;
  std::vector<CT> children_;
  bool children_setup_{false};
  bool ready_{false};
};

// === NULL BUFFER =============================================================

template <typename RT>
class NullBuffer : public Deserializer<RT> {
public:

  size_t feed(const char*&, uint64_t&) {
    return 0;
  }

  bool ready() const {
    return true;
  }

  RT get() const {
    return {};
  }

};

// =============================================================================
// === SERIALIZER ==============================================================
// =============================================================================

/**
 * Encoder serializes provided argument to Kafka format
 * In case of primitive types, this is done explicitly
 * In case of composite types (request/response), this is done by calling 'encode' on provided argument
 */

class Encoder {
public:
  template <typename T>
  size_t encode(const T& arg, char* dst);

  template <typename T>
  size_t encode(const NULLABLE_ARRAY<T>& arg, char* dst);
};

template <typename T> inline
size_t Encoder::encode(const T& arg, char* dst) {
  return arg.encode(dst, *this);
}

template <> inline
size_t Encoder::encode(const INT8& arg, char* dst) {
  memcpy(dst, &arg, sizeof(INT8));
  return sizeof(INT8);
}

#define ENCODE_NUMERIC_TYPE(TYPE, CONVERTER) \
template <> inline \
size_t Encoder::encode(const TYPE& arg, char* dst) { \
  TYPE val = CONVERTER(arg); \
  memcpy(dst, &val, sizeof(TYPE)); \
  return sizeof(TYPE); \
}

ENCODE_NUMERIC_TYPE(INT16, htobe16);
ENCODE_NUMERIC_TYPE(INT32, htobe32);
ENCODE_NUMERIC_TYPE(UINT32, htobe32);
ENCODE_NUMERIC_TYPE(INT64, htobe64);

template <> inline
size_t Encoder::encode(const BOOLEAN& arg, char* dst) {
  INT8 val = arg;
  memcpy(dst, &val, sizeof(INT8));
  return sizeof(INT8);
}

template <> inline
size_t Encoder::encode(const STRING& arg, char* dst) {
  INT16 string_length = arg.length();
  size_t header_length = encode(string_length, dst);
  memcpy(dst + header_length, arg.c_str(), string_length);
  return header_length + string_length;
}

template <> inline
size_t Encoder::encode(const NULLABLE_STRING& arg, char* dst) {
  if (arg.has_value()) {
    return encode(*arg, dst);
  } else {
    INT16 len = -1;
    return encode(len, dst);
  }
}

template <> inline
size_t Encoder::encode(const BYTES& arg, char* dst) {
  INT32 data_length = arg.size();
  size_t header_length = encode(data_length, dst);
  memcpy(dst + header_length, arg.data(), arg.size());
  return header_length + data_length;
}

template <> inline
size_t Encoder::encode(const NULLABLE_BYTES& arg, char* dst) {
  if (arg.has_value()) {
    return encode(*arg, dst);
  } else {
    INT32 len = -1;
    return encode(len, dst);
  }
}

// encoding array
template <typename T>
size_t Encoder::encode(const NULLABLE_ARRAY<T>& arg, char* dst) {
  if (arg.has_value()) {
    INT32 len = arg->size();
    size_t header_length = encode(len, dst);
    size_t written{0};
    for (const T& el : *arg) {
      written += encode(el, dst + header_length + written);
    }
    return header_length + written;
  } else {
    INT32 len = -1;
    return encode(len, dst);
  }
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
