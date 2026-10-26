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
 * RESP protocol version used by a connection.
 */
enum class RespProtocolVersion { Resp2, Resp3 };

/**
 * Maps the RESP wire version integer (the ``N`` of ``HELLO N``) to RespProtocolVersion. This is
 * a total conversion, not a validator: every value other than 3 (including 0 and values a HELLO
 * handler would reject) maps to Resp2. Callers are expected to pass versions already validated
 * at the negotiation boundary.
 */
inline RespProtocolVersion toRespProtocolVersion(uint32_t version) {
  return version == 3 ? RespProtocolVersion::Resp3 : RespProtocolVersion::Resp2;
}

inline uint32_t toWireRespVersion(RespProtocolVersion version) {
  return version == RespProtocolVersion::Resp3 ? 3u : 2u;
}

// Replace every ASCII control byte (< 0x20 or DEL 0x7f) in ``input`` with a space, returning a
// sanitized copy. RESP inline errors (``-<text>\r\n``) and the RESP2 down-converted form of a
// RESP3 BlobError have no length prefix, so an embedded CR/LF would desynchronize the downstream
// parser and other control bytes can inject terminal escapes / log delimiters once a client logs
// the message; attacker-influenced error text (echoed commands/options, upstream BlobError
// payloads) is therefore stripped. A single detection scan runs either way; when it finds no
// control bytes (the common case) the input is returned as-is, skipping the rewrite pass and its
// extra allocation.
inline std::string sanitizeControlBytes(const std::string& input) {
  const auto is_control = [](unsigned char c) { return c < 0x20 || c == 0x7f; };
  bool needs_sanitize = false;
  for (unsigned char c : input) {
    if (is_control(c)) {
      needs_sanitize = true;
      break;
    }
  }
  if (!needs_sanitize) {
    return input;
  }
  std::string out;
  out.reserve(input.size());
  for (unsigned char c : input) {
    out.push_back(is_control(c) ? ' ' : static_cast<char>(c));
  }
  return out;
}

/**
 * All RESP types as defined here: https://redis.io/topics/protocol with the exception of
 * CompositeArray. CompositeArray is an internal type that behaves like an Array type. Its first
 * element is a SimpleString or BulkString and the rest of the elements are portion of another
 * Array. This is created for performance.
 *
 * RESP3 types are defined here:
 * https://github.com/redis/redis-specifications/blob/master/protocol/RESP3.md
 */
enum class RespType {
  // RESP2 types
  Null,
  SimpleString,
  BulkString,
  Integer,
  Error,
  Array,
  CompositeArray,
  // RESP3 types
  Boolean,        // #t\r\n or #f\r\n
  Double,         // ,<floating-point-number>\r\n
  BigNumber,      // (<big number>\r\n
  BlobError,      // !<length>\r\n<error>\r\n
  VerbatimString, // =<length>\r\n<encoding>:<data>\r\n
  Map,            // %<count>\r\n (count key-value pairs, stored as 2*count array elements)
  Set,            // ~<count>\r\n
  Push,           // ><count>\r\n
};

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
  // Backed by ``string_``. Stored payload by type (no wire framing):
  //   SimpleString / BulkString / Error / BlobError: the bytes.
  //   BigNumber:       digits.
  //   VerbatimString:  ``xxx:data`` (encoder strips the ``xxx:`` prefix on RESP2).
  //   Double:          raw RESP3 Double payload — decoder → encoder pass-through preserves
  //                    upstream bytes verbatim (a non-canonical-but-parseable representation
  //                    survives intact).
  std::string& asString();
  const std::string& asString() const;
  int64_t& asInteger();
  int64_t asInteger() const;
  bool asBoolean() const;
  CompositeArray& asCompositeArray();
  const CompositeArray& asCompositeArray() const;

  /**
   * Get/set the type of the RespValue. A RespValue can only be a single type at a time. Each time
   * type() is called the type is changed and then the type specific as* methods can be used.
   */
  RespType type() const { return type_; }
  void type(RespType type);

  /**
   * @return whether this value is an error reply: a RESP2 Error or a RESP3 BlobError. The two
   *         differ only in framing (line vs length-prefixed), so reply-handling code should
   *         treat them uniformly; testing only ``RespType::Error`` silently mishandles RESP3
   *         blob errors.
   */
  bool isError() const { return type_ == RespType::Error || type_ == RespType::BlobError; }

private:
  // Double shares ``string_`` with BigNumber/VerbatimString — see ``asString()``.
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

  /**
   * Set the RESP protocol version for encoding. Drives every RESP3-only type's
   * conversion choice when the connection is RESP2:
   *   Null           - RESP3 ``_\r\n``         vs RESP2 ``$-1\r\n``
   *   Boolean        - RESP3 ``#t/#f\r\n``     vs RESP2 ``:1/:0\r\n``
   *   Double         - RESP3 ``,<digits>\r\n`` vs RESP2 ``$<len>\r\n<digits>\r\n``
   *   BigNumber      - RESP3 ``(<digits>\r\n`` vs RESP2 ``$<len>\r\n<digits>\r\n``
   *   BlobError      - RESP3 ``!<len>\r\n...`` vs RESP2 ``-<sanitized>\r\n``
   *   VerbatimString - RESP3 ``=<len>\r\n...`` vs RESP2 ``$<len>\r\n<data>\r\n``
   *                                                       (format prefix stripped)
   *   Map            - RESP3 ``%<N>\r\n``      vs RESP2 ``*<2N>\r\n`` (flat)
   *   Set            - RESP3 ``~<N>\r\n``      vs RESP2 ``*<N>\r\n``
   *   Push           - RESP3 ``><N>\r\n``      vs RESP2 ``*<N>\r\n``
   * RESP2 base types (Array, BulkString, Integer, SimpleString, Error) are
   * encoded identically in both versions and are unaffected by this setting.
   */
  virtual void setProtocolVersion(RespProtocolVersion) PURE;
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
