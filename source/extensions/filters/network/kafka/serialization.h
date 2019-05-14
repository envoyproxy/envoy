#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"
#include "envoy/common/pure.h"

#include "common/common/byte_order.h"
#include "common/common/fmt.h"

#include "extensions/filters/network/kafka/kafka_types.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Deserializer is a stateful entity that constructs a result of type T from bytes provided.
 * It can be feed()-ed data until it is ready, filling the internal store.
 * When ready(), it is safe to call get() to transform the internally stored bytes into result.
 * Further feed()-ing should have no effect on a buffer (should return 0 and not move
 * provided pointer).
 * @param T type of deserialized data.
 */
template <typename T> class Deserializer {
public:
  virtual ~Deserializer() = default;

  /**
   * Submit data to be processed, will consume as much data as it is necessary.
   * If any bytes are consumed, then the provided string view is updated by stepping over consumed
   * bytes. Invoking this method when deserializer is ready has no effect (consumes 0 bytes).
   * @param data bytes to be processed, will be updated if any have been consumed.
   * @return number of bytes consumed (equal to change in 'data').
   */
  virtual uint32_t feed(absl::string_view& data) PURE;

  /**
   * Whether deserializer has consumed enough data to return result.
   */
  virtual bool ready() const PURE;

  /**
   * Returns the entity that is represented by bytes stored in this deserializer.
   * Should be only called when deserializer is ready.
   */
  virtual T get() const PURE;
};

/**
 * Generic integer deserializer (uses array of sizeof(T) bytes).
 * After all bytes are filled in, the value is converted from network byte-order and returned.
 */
template <typename T> class IntDeserializer : public Deserializer<T> {
public:
  IntDeserializer() : written_{0}, ready_(false){};

  uint32_t feed(absl::string_view& data) override {
    const uint32_t available = std::min<uint32_t>(sizeof(buf_) - written_, data.size());
    memcpy(buf_ + written_, data.data(), available);
    written_ += available;

    if (written_ == sizeof(buf_)) {
      ready_ = true;
    }

    data = {data.data() + available, data.size() - available};

    return available;
  }

  bool ready() const override { return ready_; }

protected:
  char buf_[sizeof(T) / sizeof(char)];
  uint32_t written_;
  bool ready_{false};
};

/**
 * Integer deserializer for int8_t.
 */
class Int8Deserializer : public IntDeserializer<int8_t> {
public:
  int8_t get() const override {
    int8_t result;
    memcpy(&result, buf_, sizeof(result));
    return result;
  }
};

/**
 * Integer deserializer for int16_t.
 */
class Int16Deserializer : public IntDeserializer<int16_t> {
public:
  int16_t get() const override {
    int16_t result;
    memcpy(&result, buf_, sizeof(result));
    return be16toh(result);
  }
};

/**
 * Integer deserializer for int32_t.
 */
class Int32Deserializer : public IntDeserializer<int32_t> {
public:
  int32_t get() const override {
    int32_t result;
    memcpy(&result, buf_, sizeof(result));
    return be32toh(result);
  }
};

/**
 * Integer deserializer for uint32_t.
 */
class UInt32Deserializer : public IntDeserializer<uint32_t> {
public:
  uint32_t get() const override {
    uint32_t result;
    memcpy(&result, buf_, sizeof(result));
    return be32toh(result);
  }
};

/**
 * Integer deserializer for uint64_t.
 */
class Int64Deserializer : public IntDeserializer<int64_t> {
public:
  int64_t get() const override {
    int64_t result;
    memcpy(&result, buf_, sizeof(result));
    return be64toh(result);
  }
};

/**
 * Deserializer for boolean values.
 * Uses a single int8 deserializer, and checks whether the results equals 0.
 * When reading a boolean value, any non-zero value is considered true.
 * Impl note: could have been a subclass of IntDeserializer<int8_t> with a different get function,
 * but it makes it harder to understand.
 */
class BooleanDeserializer : public Deserializer<bool> {
public:
  BooleanDeserializer(){};

  uint32_t feed(absl::string_view& data) override { return buffer_.feed(data); }

  bool ready() const override { return buffer_.ready(); }

  bool get() const override { return 0 != buffer_.get(); }

private:
  Int8Deserializer buffer_;
};

/**
 * Deserializer of string value.
 * First reads length (INT16) and then allocates the buffer of given length.
 *
 * From Kafka documentation:
 * First the length N is given as an INT16.
 * Then N bytes follow which are the UTF-8 encoding of the character sequence.
 * Length must not be negative.
 */
class StringDeserializer : public Deserializer<std::string> {
public:
  /**
   * Can throw EnvoyException if given string length is not valid.
   */
  uint32_t feed(absl::string_view& data) override;

  bool ready() const override { return ready_; }

  std::string get() const override { return std::string(data_buf_.begin(), data_buf_.end()); }

private:
  Int16Deserializer length_buf_;
  bool length_consumed_{false};

  int16_t required_;
  std::vector<char> data_buf_;

  bool ready_{false};
};

/**
 * Deserializer of nullable string value.
 * First reads length (INT16) and then allocates the buffer of given length.
 * If length was -1, buffer allocation is omitted and deserializer is immediately ready (returning
 * null value).
 *
 * From Kafka documentation:
 * For non-null strings, first the length N is given as an INT16.
 * Then N bytes follow which are the UTF-8 encoding of the character sequence.
 * A null value is encoded with length of -1 and there are no following bytes.
 */
class NullableStringDeserializer : public Deserializer<NullableString> {
public:
  /**
   * Can throw EnvoyException if given string length is not valid.
   */
  uint32_t feed(absl::string_view& data) override;

  bool ready() const override { return ready_; }

  NullableString get() const override {
    return required_ >= 0 ? absl::make_optional(std::string(data_buf_.begin(), data_buf_.end()))
                          : absl::nullopt;
  }

private:
  Int16Deserializer length_buf_;
  bool length_consumed_{false};

  int16_t required_;
  std::vector<char> data_buf_;

  bool ready_{false};
};

/**
 * Deserializer of bytes value.
 * First reads length (INT32) and then allocates the buffer of given length.
 *
 * From Kafka documentation:
 * First the length N is given as an INT32. Then N bytes follow.
 */
class BytesDeserializer : public Deserializer<Bytes> {
public:
  /**
   * Can throw EnvoyException if given bytes length is not valid.
   */
  uint32_t feed(absl::string_view& data) override;

  bool ready() const override { return ready_; }

  Bytes get() const override { return data_buf_; }

private:
  Int32Deserializer length_buf_;
  bool length_consumed_{false};
  int32_t required_;

  std::vector<unsigned char> data_buf_;
  bool ready_{false};
};

/**
 * Deserializer of nullable bytes value.
 * First reads length (INT32) and then allocates the buffer of given length.
 * If length was -1, buffer allocation is omitted and deserializer is immediately ready (returning
 * null value).
 *
 * From Kafka documentation:
 * For non-null values, first the length N is given as an INT32. Then N bytes follow.
 * A null value is encoded with length of -1 and there are no following bytes.
 */
class NullableBytesDeserializer : public Deserializer<NullableBytes> {
public:
  /**
   * Can throw EnvoyException if given bytes length is not valid.
   */
  uint32_t feed(absl::string_view& data) override;

  bool ready() const override { return ready_; }

  NullableBytes get() const override {
    return required_ >= 0 ? absl::make_optional(data_buf_) : absl::nullopt;
  }

private:
  Int32Deserializer length_buf_;
  bool length_consumed_{false};
  int32_t required_;

  std::vector<unsigned char> data_buf_;
  bool ready_{false};
};

/**
 * Deserializer for array of objects of the same type.
 *
 * First reads the length of the array, then initializes N underlying deserializers of type
 * DeserializerType. After the last of N deserializers is ready, the results of each of them are
 * gathered and put in a vector.
 * @param ResponseType result type returned by deserializer of type DeserializerType.
 * @param DeserializerType underlying deserializer type.
 *
 * From Kafka documentation:
 * Represents a sequence of objects of a given type T. Type T can be either a primitive type (e.g.
 * STRING) or a structure. First, the length N is given as an int32_t. Then N instances of type T
 * follow. A null array is represented with a length of -1.
 */
template <typename ResponseType, typename DeserializerType>
class ArrayDeserializer : public Deserializer<std::vector<ResponseType>> {
public:
  /**
   * Can throw EnvoyException if array length is invalid or if underlying deserializer can throw.
   */
  uint32_t feed(absl::string_view& data) override {

    const uint32_t length_consumed = length_buf_.feed(data);
    if (!length_buf_.ready()) {
      // Break early: we still need to fill in length buffer.
      return length_consumed;
    }

    if (!length_consumed_) {
      required_ = length_buf_.get();
      if (required_ >= 0) {
        children_ = std::vector<DeserializerType>(required_);
      } else {
        throw EnvoyException(fmt::format("invalid ARRAY length: {}", required_));
      }
      length_consumed_ = true;
    }

    if (ready_) {
      return length_consumed;
    }

    uint32_t child_consumed{0};
    for (DeserializerType& child : children_) {
      child_consumed += child.feed(data);
    }

    bool children_ready_ = true;
    for (DeserializerType& child : children_) {
      children_ready_ &= child.ready();
    }
    ready_ = children_ready_;

    return length_consumed + child_consumed;
  }

  bool ready() const override { return ready_; }

  std::vector<ResponseType> get() const override {
    std::vector<ResponseType> result{};
    result.reserve(children_.size());
    for (const DeserializerType& child : children_) {
      const ResponseType child_result = child.get();
      result.push_back(child_result);
    }
    return result;
  }

private:
  Int32Deserializer length_buf_;
  bool length_consumed_{false};
  int32_t required_;
  std::vector<DeserializerType> children_;
  bool children_setup_{false};
  bool ready_{false};
};

/**
 * Deserializer for nullable array of objects of the same type.
 *
 * First reads the length of the array, then initializes N underlying deserializers of type
 * DeserializerType. After the last of N deserializers is ready, the results of each of them are
 * gathered and put in a vector.
 * @param ResponseType result type returned by deserializer of type DeserializerType.
 * @param DeserializerType underlying deserializer type.
 *
 * From Kafka documentation:
 * Represents a sequence of objects of a given type T. Type T can be either a primitive type (e.g.
 * STRING) or a structure. First, the length N is given as an int32_t. Then N instances of type T
 * follow. A null array is represented with a length of -1.
 */
template <typename ResponseType, typename DeserializerType>
class NullableArrayDeserializer : public Deserializer<NullableArray<ResponseType>> {
public:
  /**
   * Can throw EnvoyException if array length is invalid or if underlying deserializer can throw.
   */
  uint32_t feed(absl::string_view& data) override {

    const uint32_t length_consumed = length_buf_.feed(data);
    if (!length_buf_.ready()) {
      // Break early: we still need to fill in length buffer.
      return length_consumed;
    }

    if (!length_consumed_) {
      required_ = length_buf_.get();

      if (required_ >= 0) {
        children_ = std::vector<DeserializerType>(required_);
      }
      if (required_ == NULL_ARRAY_LENGTH) {
        ready_ = true;
      }
      if (required_ < NULL_ARRAY_LENGTH) {
        throw EnvoyException(fmt::format("invalid NULLABLE_ARRAY length: {}", required_));
      }

      length_consumed_ = true;
    }

    if (ready_) {
      return length_consumed;
    }

    uint32_t child_consumed{0};
    for (DeserializerType& child : children_) {
      child_consumed += child.feed(data);
    }

    bool children_ready_ = true;
    for (DeserializerType& child : children_) {
      children_ready_ &= child.ready();
    }
    ready_ = children_ready_;

    return length_consumed + child_consumed;
  }

  bool ready() const override { return ready_; }

  NullableArray<ResponseType> get() const override {
    if (NULL_ARRAY_LENGTH != required_) {
      std::vector<ResponseType> result{};
      result.reserve(children_.size());
      for (const DeserializerType& child : children_) {
        const ResponseType child_result = child.get();
        result.push_back(child_result);
      }
      return result;
    } else {
      return absl::nullopt;
    }
  }

private:
  constexpr static int32_t NULL_ARRAY_LENGTH{-1};

  Int32Deserializer length_buf_;
  bool length_consumed_{false};
  int32_t required_;
  std::vector<DeserializerType> children_;
  bool children_setup_{false};
  bool ready_{false};
};

/**
 * Encodes provided argument in Kafka format.
 * In case of primitive types, this is done explicitly as per specification.
 * In case of composite types, this is done by calling 'encode' on provided argument.
 *
 * This object also carries extra information that is used while traversing the request
 * structure-tree during encoding (currently api_version, as different request versions serialize
 * differently).
 */
// TODO(adamkotwasinski) that class might be split into Request/ResponseEncodingContext in future
class EncodingContext {
public:
  EncodingContext(int16_t api_version) : api_version_{api_version} {};

  /**
   * Compute size of given reference, if it were to be encoded.
   * @return serialized size of argument.
   */
  template <typename T> uint32_t computeSize(const T& arg) const;

  /**
   * Compute size of given array, if it were to be encoded.
   * @return serialized size of argument.
   */
  template <typename T> uint32_t computeSize(const std::vector<T>& arg) const;

  /**
   * Compute size of given nullable array, if it were to be encoded.
   * @return serialized size of argument.
   */
  template <typename T> uint32_t computeSize(const NullableArray<T>& arg) const;

  /**
   * Encode given reference in a buffer.
   * @return bytes written
   */
  template <typename T> uint32_t encode(const T& arg, Buffer::Instance& dst);

  /**
   * Encode given array in a buffer.
   * @return bytes written
   */
  template <typename T> uint32_t encode(const std::vector<T>& arg, Buffer::Instance& dst);

  /**
   * Encode given nullable array in a buffer.
   * @return bytes written
   */
  template <typename T> uint32_t encode(const NullableArray<T>& arg, Buffer::Instance& dst);

  int16_t apiVersion() const { return api_version_; }

private:
  const int16_t api_version_;
};

/**
 * For non-primitive types, call `computeSize` on them, to delegate the work to the entity itself.
 * The entity may use the information in context to decide which fields are included etc.
 */
template <typename T> inline uint32_t EncodingContext::computeSize(const T& arg) const {
  return arg.computeSize(*this);
}

/**
 * For primitive types, Kafka size == sizeof(x).
 */
#define COMPUTE_SIZE_OF_NUMERIC_TYPE(TYPE)                                                         \
  template <> constexpr uint32_t EncodingContext::computeSize(const TYPE&) const {                 \
    return sizeof(TYPE);                                                                           \
  }

COMPUTE_SIZE_OF_NUMERIC_TYPE(bool)
COMPUTE_SIZE_OF_NUMERIC_TYPE(int8_t)
COMPUTE_SIZE_OF_NUMERIC_TYPE(int16_t)
COMPUTE_SIZE_OF_NUMERIC_TYPE(int32_t)
COMPUTE_SIZE_OF_NUMERIC_TYPE(uint32_t)
COMPUTE_SIZE_OF_NUMERIC_TYPE(int64_t)

/**
 * Template overload for string.
 * Kafka String's size is INT16 for header + N bytes.
 */
template <> inline uint32_t EncodingContext::computeSize(const std::string& arg) const {
  return sizeof(int16_t) + arg.size();
}

/**
 * Template overload for nullable string.
 * Kafka NullableString's size is INT16 for header + N bytes (N >= 0).
 */
template <> inline uint32_t EncodingContext::computeSize(const NullableString& arg) const {
  return sizeof(int16_t) + (arg ? arg->size() : 0);
}

/**
 * Template overload for byte array.
 * Kafka byte array size is INT32 for header + N bytes.
 */
template <> inline uint32_t EncodingContext::computeSize(const Bytes& arg) const {
  return sizeof(int32_t) + arg.size();
}

/**
 * Template overload for nullable byte array.
 * Kafka nullable byte array size is INT32 for header + N bytes (N >= 0).
 */
template <> inline uint32_t EncodingContext::computeSize(const NullableBytes& arg) const {
  return sizeof(int32_t) + (arg ? arg->size() : 0);
}

/**
 * Template overload for Array of T.
 * The size of array is size of header and all of its elements.
 */
template <typename T>
inline uint32_t EncodingContext::computeSize(const std::vector<T>& arg) const {
  uint32_t result = sizeof(int32_t);
  for (const T& el : arg) {
    result += computeSize(el);
  }
  return result;
}

/**
 * Template overload for NullableArray of T.
 * The size of array is size of header and all of its elements.
 */
template <typename T>
inline uint32_t EncodingContext::computeSize(const NullableArray<T>& arg) const {
  return arg ? computeSize(*arg) : sizeof(int32_t);
}

/**
 * For non-primitive types, call `encode` on them, to delegate the serialization to the entity
 * itself.
 */
template <typename T> inline uint32_t EncodingContext::encode(const T& arg, Buffer::Instance& dst) {
  return arg.encode(dst, *this);
}

/**
 * Template overload for int8_t.
 * Encode a single byte.
 */
template <> inline uint32_t EncodingContext::encode(const int8_t& arg, Buffer::Instance& dst) {
  dst.add(&arg, sizeof(int8_t));
  return sizeof(int8_t);
}

/**
 * Template overload for int16_t, int32_t, uint32_t, int64_t.
 * Encode a N-byte integer, converting to network byte-order.
 */
#define ENCODE_NUMERIC_TYPE(TYPE, CONVERTER)                                                       \
  template <> inline uint32_t EncodingContext::encode(const TYPE& arg, Buffer::Instance& dst) {    \
    const TYPE val = CONVERTER(arg);                                                               \
    dst.add(&val, sizeof(TYPE));                                                                   \
    return sizeof(TYPE);                                                                           \
  }

ENCODE_NUMERIC_TYPE(int16_t, htobe16);
ENCODE_NUMERIC_TYPE(int32_t, htobe32);
ENCODE_NUMERIC_TYPE(uint32_t, htobe32);
ENCODE_NUMERIC_TYPE(int64_t, htobe64);

/**
 * Template overload for bool.
 * Encode boolean as a single byte.
 */
template <> inline uint32_t EncodingContext::encode(const bool& arg, Buffer::Instance& dst) {
  int8_t val = arg;
  dst.add(&val, sizeof(int8_t));
  return sizeof(int8_t);
}

/**
 * Template overload for std::string.
 * Encode string as INT16 length + N bytes.
 */
template <> inline uint32_t EncodingContext::encode(const std::string& arg, Buffer::Instance& dst) {
  int16_t string_length = arg.length();
  uint32_t header_length = encode(string_length, dst);
  dst.add(arg.c_str(), string_length);
  return header_length + string_length;
}

/**
 * Template overload for NullableString.
 * Encode nullable string as INT16 length + N bytes (length = -1 for null).
 */
template <>
inline uint32_t EncodingContext::encode(const NullableString& arg, Buffer::Instance& dst) {
  if (arg.has_value()) {
    return encode(*arg, dst);
  } else {
    const int16_t len = -1;
    return encode(len, dst);
  }
}

/**
 * Template overload for Bytes.
 * Encode byte array as INT32 length + N bytes.
 */
template <> inline uint32_t EncodingContext::encode(const Bytes& arg, Buffer::Instance& dst) {
  const int32_t data_length = arg.size();
  const uint32_t header_length = encode(data_length, dst);
  dst.add(arg.data(), arg.size());
  return header_length + data_length;
}

/**
 * Template overload for NullableBytes.
 * Encode nullable byte array as INT32 length + N bytes (length = -1 for null value).
 */
template <>
inline uint32_t EncodingContext::encode(const NullableBytes& arg, Buffer::Instance& dst) {
  if (arg.has_value()) {
    return encode(*arg, dst);
  } else {
    const int32_t len = -1;
    return encode(len, dst);
  }
}

/**
 * Encode nullable object array to T as INT32 length + N elements.
 * Each element of type T then serializes itself on its own.
 */
template <typename T>
uint32_t EncodingContext::encode(const std::vector<T>& arg, Buffer::Instance& dst) {
  const NullableArray<T> wrapped = {arg};
  return encode(wrapped, dst);
}

/**
 * Encode nullable object array to T as INT32 length + N elements (length = -1 for null value).
 * Each element of type T then serializes itself on its own.
 */
template <typename T>
uint32_t EncodingContext::encode(const NullableArray<T>& arg, Buffer::Instance& dst) {
  if (arg.has_value()) {
    const int32_t len = arg->size();
    const uint32_t header_length = encode(len, dst);
    uint32_t written{0};
    for (const T& el : *arg) {
      // For each of array elements, resolve the correct method again.
      // Elements could be primitives or complex types, so calling encode() on object won't work.
      written += encode(el, dst);
    }
    return header_length + written;
  } else {
    const int32_t len = -1;
    return encode(len, dst);
  }
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
