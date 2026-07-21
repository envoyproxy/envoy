#pragma once

#include <cstdint>
#include <forward_list>
#include <string>
#include <vector>

#include "source/common/common/logger.h"
#include "source/extensions/filters/network/common/redis/codec.h"

#include "absl/types/span.h"

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

  // DoS-defense limits applied to a single decoded message. Exposed for tests so limit-boundary
  // cases reference them by name instead of hardcoding these implementation values; they are not a
  // stability contract for callers.
  //
  // Maximum elements in one aggregate (``*``/``%``/``~``/``>``). Bounds per-aggregate allocation;
  // matches upstream's array size upper bound (``MaxArraySize``).
  static constexpr uint64_t kMaxRespElements = 2000000;
  // Maximum bytes for a single bulk string ($), blob error (!), or verbatim string (=). Matches
  // Redis's ``proto-max-bulk-len`` default of 512 MiB; rejected before the body-read loop so an
  // attacker-supplied length header cannot drive unbounded ``std::string::append`` growth.
  static constexpr uint64_t kMaxBulkStringLength = 512ULL * 1024 * 1024;
  // Maximum bytes of a RESP3 Double (``,``) payload. A double is a small fixed-width scalar, so 64
  // bytes is plenty; the cap exists to bound unbounded CRLF-less line growth.
  static constexpr size_t kMaxDoubleTokenLength = 64;
  // Maximum bytes of a RESP3 BigNumber (``(``) payload. BigNumber is arbitrary-precision, so a long
  // value is not malformed; the cap (16 KiB, room for a ~54k-bit integer) only bounds unbounded
  // CRLF-less line growth without rejecting legitimate values.
  static constexpr size_t kMaxBigNumberTokenLength = 16ULL * 1024;
  // Cap on consecutive RESP3 Attributes, empty (``|0``) or not — child values completing
  // INSIDE an attribute do not reset the run (see ValueComplete), so ``|1 <k> <v>`` chains are
  // bounded the same way an empty-attribute flood is.
  static constexpr uint32_t kMaxConsecutiveAttributes = 32;
  // Cap on aggregate nesting depth. Without this, ``*1\r\n*1\r\n...`` grows pending_value_stack_
  // without bound even though each level's own kMaxRespElements check passes trivially. Tracked in
  // a counter because std::forward_list has no O(1) size(). Same value as upstream's
  // ``MaxArrayNestingDepth``; the accepted depth is one level tighter (128 vs upstream's 129)
  // because the pre-push check rejects at ``>= kMaxNestingDepth`` where upstream used ``>``
  // (both counts include the root frame).
  static constexpr uint32_t kMaxNestingDepth = 128;
  // Cap on cumulative RespValue slots reserved across all aggregate levels of one root value. No
  // upstream equivalent: an additional defense against multiplicative growth (kMaxNestingDepth *
  // kMaxRespElements element headers) that no single per-aggregate / per-depth limit catches.
  static constexpr uint64_t kMaxTotalElements = 4ULL * 1024ULL * 1024ULL;

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
  // Scratch buffer for the RESP3 Double (``,``) payload, which re-uses the SimpleString parse
  // states but accumulates here. Whitespace and non-printable bytes are rejected and the length
  // cap enforced as each byte is accumulated; the terminating CR validates numeric syntax and
  // moves the buffer into ``RespValue::asString()``.
  std::string pending_double_buf_;
  uint32_t consecutive_attributes_{0}; // counts toward kMaxConsecutiveAttributes
  // Number of attribute frames currently open on pending_value_stack_. Values completing while
  // this is non-zero belong to a frame that will itself be discarded, so they must not reset
  // the consecutive-attribute counter (see ValueComplete).
  uint32_t open_attribute_frames_{0};
  uint32_t pending_value_stack_depth_{0}; // counts toward kMaxNestingDepth
  uint64_t total_elements_{0};            // counts toward kMaxTotalElements
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
  // Aggregate encoders take a span so a caller can pass either the stored vector directly or a
  // truncated view of it (see the Map case in encode(), which enforces the even-length
  // invariant once and hands both the RESP3 and RESP2 paths the same view).
  void encodeArray(absl::Span<const RespValue> array, Buffer::Instance& out);
  void encodeCompositeArray(const RespValue::CompositeArray& array, Buffer::Instance& out);
  void encodeBulkString(const std::string& string, Buffer::Instance& out);
  // Shared body of the three length-prefixed scalar frames — ``$`` bulk string, ``!`` blob
  // error, ``=`` verbatim string — which differ only in the type marker:
  // <prefix><length>CRLF<payload>CRLF.
  void encodeLengthPrefixed(char prefix, const std::string& string, Buffer::Instance& out);
  void encodeError(const std::string& string, Buffer::Instance& out);
  void encodeInteger(int64_t integer, Buffer::Instance& out);
  void encodeSimpleString(const std::string& string, Buffer::Instance& out);
  // RESP3 encoders
  void encodeBoolean(bool value, Buffer::Instance& out);
  void encodeBigNumber(const std::string& value, Buffer::Instance& out);
  void encodeBlobError(const std::string& error, Buffer::Instance& out);
  void encodeVerbatimString(const std::string& string, Buffer::Instance& out);
  void encodeAggregate(char prefix, absl::Span<const RespValue> array, Buffer::Instance& out);
  void encodeMap(absl::Span<const RespValue> array, Buffer::Instance& out);
  void encodeSet(absl::Span<const RespValue> array, Buffer::Instance& out);
  void encodePush(absl::Span<const RespValue> array, Buffer::Instance& out);
  void encodeNull(Buffer::Instance& out);
};

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
