#pragma once

#include <cstdint>
#include <forward_list>
#include <string>
#include <vector>

#include "source/common/common/logger.h"
#include "source/extensions/filters/network/common/redis/codec.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

/**
 * Decoder implementation of https://redis.io/topics/protocol
 *
 * This implementation buffers when needed and will always consume all bytes passed for decoding.
 */
class DecoderImpl : public Decoder, Logger::Loggable<Logger::Id::redis> {
public:
  DecoderImpl(DecoderCallbacks& callbacks) : callbacks_(callbacks) {}

  // RedisProxy::Decoder
  void decode(Buffer::Instance& data) override;

private:
  enum class State {
    ValueRootStart,
    ValueStart,
    IntegerStart,
    Integer,
    IntegerLF,
    BulkStringBody,
    CR,
    LF,
    SimpleString,
    ValueComplete,
    InlineStart,
    InlineDelimiter,
    InlineString,
    InlineStringQuoted,
    InlineStringQuotedEscape,
    InlineStringQuotedEscapeHex,
    InlineStringSingleQuoted,
    InlineStringSingleQuotedEscape,
    // RESP3 states
    BooleanValue, // Expecting 't' or 'f' after '#'
  };

  struct PendingInteger {
    void reset() {
      integer_ = 0;
      negative_ = false;
      digit_seen_ = false;
    }

    uint64_t integer_;
    bool negative_;
    // Tracks whether the integer/length parser saw at least one decimal digit between the
    // type byte (and optional ``-``) and the terminating ``\r``. Without this, malformed
    // forms like ``:\r\n``, ``:-\r\n``, ``*\r\n``, ``$\r\n`` would be accepted as zero
    // (silently coerced to a 0 integer / empty array / zero-length bulk), letting an
    // attacker inject ambiguous frames the rest of the parser cannot distinguish from a
    // legitimate ``:0`` / ``*0`` / ``$0``. Reject at IntegerLF entry instead.
    bool digit_seen_;
  };

  struct PendingValue {
    RespValue* value_;
    uint64_t current_array_element_;
    bool is_attribute_{false}; // True when parsing RESP3 Attribute (|) — discarded on completion.
  };

  void parseSlice(const Buffer::RawSlice& slice);

  DecoderCallbacks& callbacks_;
  State state_{State::ValueRootStart};
  PendingInteger pending_integer_;
  RespValuePtr pending_value_root_;
  std::forward_list<PendingValue> pending_value_stack_;
  // Temporary buffer for accumulating RESP3 Double values during parsing.
  // The `,` prefix uses SimpleString state to accumulate characters, then
  // the buffer is parsed to double at ValueComplete.
  std::string pending_double_buf_;
  // Guard against DoS via chains of empty RESP3 Attributes (|0\r\n|0\r\n...).
  static constexpr uint32_t kMaxConsecutiveAttributes = 32;
  uint32_t consecutive_attributes_{0};
  // Cap aggregate nesting depth. Without this, a hostile peer can send
  // *1\r\n*1\r\n*1\r\n... and grow pending_value_stack_ without bound,
  // even though each level's own kMaxRespElements check passes trivially.
  // We track depth in a counter rather than calling size() because
  // std::forward_list does not support O(1) size().
  static constexpr uint32_t kMaxNestingDepth = 32;
  uint32_t pending_value_stack_depth_{0};
  // Cap the total number of RespValue slots reserved across all aggregate
  // levels of one root-level value. Multiplicative growth (32 * 1M) without
  // this cap can demand tens of GB of memory before any single per-aggregate
  // limit fires.
  static constexpr uint64_t kMaxTotalElements = 4ULL * 1024ULL * 1024ULL;
  uint64_t total_elements_{0};
};

/**
 * A factory implementation that returns a real decoder.
 */
class DecoderFactoryImpl : public DecoderFactory {
public:
  // RedisProxy::DecoderFactory
  DecoderPtr create(DecoderCallbacks& callbacks) override {
    return DecoderPtr{new DecoderImpl(callbacks)};
  }
};

/**
 * Encoder implementation of https://redis.io/topics/protocol
 */
class EncoderImpl : public Encoder {
public:
  // RedisProxy::Encoder
  void encode(const RespValue& value, Buffer::Instance& out) override;
  void setProtocolVersion(RespProtocolVersion version) override { protocol_version_ = version; }

private:
  RespProtocolVersion protocol_version_{RespProtocolVersion::Resp2};
  void encodeArray(const std::vector<RespValue>& array, Buffer::Instance& out);
  void encodeCompositeArray(const RespValue::CompositeArray& array, Buffer::Instance& out);
  void encodeBulkString(const std::string& string, Buffer::Instance& out);
  void encodeError(const std::string& string, Buffer::Instance& out);
  void encodeInteger(int64_t integer, Buffer::Instance& out);
  void encodeSimpleString(const std::string& string, Buffer::Instance& out);
  // RESP3 encoders
  void encodeBoolean(bool value, Buffer::Instance& out);
  void encodeDouble(double value, Buffer::Instance& out);
  void encodeBigNumber(const std::string& value, Buffer::Instance& out);
  void encodeBlobError(const std::string& error, Buffer::Instance& out);
  void encodeVerbatimString(const std::string& string, Buffer::Instance& out);
  void encodeAggregate(char prefix, const std::vector<RespValue>& array, Buffer::Instance& out);
  void encodeMap(const std::vector<RespValue>& array, Buffer::Instance& out);
  void encodeSet(const std::vector<RespValue>& array, Buffer::Instance& out);
  void encodePush(const std::vector<RespValue>& array, Buffer::Instance& out);
  void encodeNull(Buffer::Instance& out);

  // RESP2 inline errors have no length prefix, so a hostile BlobError message
  // can both desynchronize downstream parsers (via embedded CR/LF) and inject
  // terminal escapes / log delimiters into a downstream's logger (via NUL,
  // BEL, ESC, DEL, ...). Strip ALL ASCII control bytes (< 0x20 or 0x7f),
  // replacing each with a space, before emitting via encodeError(). Not
  // needed for RESP3 wire forms because BlobError is length-prefixed.
  static std::string sanitizeErrorForResp2(absl::string_view input);

  // Shared decimal formatting for RESP3 Double on both the native ',...\r\n'
  // emit path and the RESP2 conversion path (bulk string of the same
  // digits without the ',' prefix).
  static std::string formatDoubleForWire(double value);
};

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
