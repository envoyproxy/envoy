#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

/**
 * All RESP types as defined here: https://redis.io/topics/protocol with the exception of
 * CompositeArray. CompositeArray is an internal type that behaves like an Array type. Its first
 * element is a SimpleString or BulkString and the rest of the elements are portion of another
 * Array. This is created for performance.
 */
enum class RespType { Null, SimpleString, BulkString, Integer, Error, Array, CompositeArray };

/**
 * A variant implementation of a RESP value optimized for performance. A C++11 union is used for
 * the underlying type so that no unnecessary allocations/constructions are needed.
 */
class RespValue {
public:
  RespValue() : type_(RespType::Null) {}

  RespValue(std::shared_ptr<RespValue> base_array, const RespValue& command, const uint64_t start,
            const uint64_t end)
      : type_(RespType::CompositeArray) {
    new (&composite_array_) CompositeArray(std::move(base_array), command, start, end);
  }
  virtual ~RespValue() { cleanup(); }

  RespValue(const RespValue& other);                // copy constructor
  RespValue(RespValue&& other) noexcept;            // move constructor
  RespValue& operator=(const RespValue& other);     // copy assignment
  RespValue& operator=(RespValue&& other) noexcept; // move assignment
  bool operator==(const RespValue& other) const;    // test for equality, unit tests
  bool operator!=(const RespValue& other) const { return !(*this == other); }

  /**
   * Convert a RESP value to a string for debugging purposes.
   */
  std::string toString() const;

  /**
   * Holds the data for CompositeArray RespType
   */
  class CompositeArray {
  public:
    CompositeArray() = default;
    CompositeArray(std::shared_ptr<RespValue> base_array, const RespValue& command,
                   const uint64_t start, const uint64_t end)
        : base_array_(std::move(base_array)), command_(&command), start_(start), end_(end) {
      ASSERT(command.type() == RespType::BulkString || command.type() == RespType::SimpleString);
      ASSERT(base_array_ != nullptr);
      ASSERT(base_array_->type() == RespType::Array);
      ASSERT(start <= end);
      ASSERT(end < base_array_->asArray().size());
    }

    const RespValue* command() const { return command_; }
    const std::shared_ptr<RespValue>& baseArray() const { return base_array_; }

    bool operator==(const CompositeArray& other) const;

    uint64_t size() const;

    /**
     * Forward const iterator for CompositeArray.
     * @note this implementation currently supports the minimum functionality needed to support
     *       the `for (const RespValue& value : array)` idiom.
     */
    struct CompositeArrayConstIterator {
      CompositeArrayConstIterator(const RespValue* command, const std::vector<RespValue>& array,
                                  uint64_t index, bool first)
          : command_(command), array_(array), index_(index), first_(first) {}
      const RespValue& operator*();
      CompositeArrayConstIterator& operator++();
      bool operator!=(const CompositeArrayConstIterator& rhs) const;
      static const CompositeArrayConstIterator& empty();

      const RespValue* command_;
      const std::vector<RespValue>& array_;
      uint64_t index_;
      bool first_;
    };

    CompositeArrayConstIterator begin() const noexcept {
      return (command_ && base_array_)
                 ? CompositeArrayConstIterator{command_, base_array_->asArray(), start_, true}
                 : CompositeArrayConstIterator::empty();
    }

    CompositeArrayConstIterator end() const noexcept {
      return (command_ && base_array_)
                 ? CompositeArrayConstIterator{command_, base_array_->asArray(), end_ + 1, false}
                 : CompositeArrayConstIterator::empty();
    }

  private:
    std::shared_ptr<RespValue> base_array_;
    const RespValue* command_;
    uint64_t start_;
    uint64_t end_;
  };

  /**
   * The following are getters and setters for the internal value. A RespValue starts as null,
   * and must change type via type() before the following methods can be used.
   */
  std::vector<RespValue>& asArray();
  const std::vector<RespValue>& asArray() const;
  std::string& asString();
  const std::string& asString() const;
  int64_t& asInteger();
  int64_t asInteger() const;
  CompositeArray& asCompositeArray();
  const CompositeArray& asCompositeArray() const;

  /**
   * Get/set the type of the RespValue. A RespValue can only be a single type at a time. Each time
   * type() is called the type is changed and then the type specific as* methods can be used.
   */
  RespType type() const { return type_; }
  void type(RespType type);

private:
  union {
    std::vector<RespValue> array_;
    std::string string_;
    int64_t integer_;
    CompositeArray composite_array_;
  };

  void cleanup();

  RespType type_{};
};

using RespValuePtr = std::unique_ptr<RespValue>;
using RespValueSharedPtr = std::shared_ptr<RespValue>;
using RespValueConstSharedPtr = std::shared_ptr<const RespValue>;

/**
 * Callbacks that the decoder fires.
 */
class DecoderCallbacks {
public:
  virtual ~DecoderCallbacks() = default;

  /**
   * Called when a new top level RESP value has been decoded. This value may include multiple
   * sub-values in the case of arrays or nested arrays.
   * @param value supplies the decoded value that is now owned by the callee.
   */
  virtual void onRespValue(RespValuePtr&& value) PURE;
};

/**
 * A redis byte decoder for https://redis.io/topics/protocol
 */
class Decoder {
public:
  virtual ~Decoder() = default;

  /**
   * Decode redis protocol bytes.
   * @param data supplies the data to decode. All bytes will be consumed by the decoder or a
   *        ProtocolError will be thrown.
   */
  virtual void decode(Buffer::Instance& data) PURE;
};

using DecoderPtr = std::unique_ptr<Decoder>;

/**
 * A factory for a redis decoder.
 */
class DecoderFactory {
public:
  virtual ~DecoderFactory() = default;

  /**
   * Create a decoder given a set of decoder callbacks.
   */
  virtual DecoderPtr create(DecoderCallbacks& callbacks) PURE;
};

/**
 * A redis byte encoder for https://redis.io/topics/protocol
 */
class Encoder {
public:
  virtual ~Encoder() = default;

  /**
   * Encode a RESP value to a buffer.
   * @param value supplies the value to encode.
   * @param out supplies the buffer to encode to.
   */
  virtual void encode(const RespValue& value, Buffer::Instance& out) PURE;
};

using EncoderPtr = std::unique_ptr<Encoder>;

/**
 * A redis protocol error.
 */
class ProtocolError : public EnvoyException {
public:
  ProtocolError(const std::string& error) : EnvoyException(error) {}
};

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
