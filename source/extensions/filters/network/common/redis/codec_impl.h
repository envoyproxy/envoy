#pragma once

#include <cstdint>
#include <forward_list>
#include <string>
#include <vector>

#include "common/common/logger.h"

#include "extensions/filters/network/common/redis/codec.h"

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
    ValueComplete
  };

  struct PendingInteger {
    void reset() {
      integer_ = 0;
      negative_ = false;
    }

    uint64_t integer_;
    bool negative_;
  };

  struct PendingValue {
    RespValue* value_;
    uint64_t current_array_element_;
  };

  void parseSlice(const Buffer::RawSlice& slice);

  DecoderCallbacks& callbacks_;
  State state_{State::ValueRootStart};
  PendingInteger pending_integer_;
  RespValuePtr pending_value_root_;
  std::forward_list<PendingValue> pending_value_stack_;
};

/**
 * Decoder implementation of https://github.com/memcached/memcached/blob/master/doc/protocol.txt
 *
 * This implementation buffers when needed and will always consume all bytes passed for decoding.
 */
class MemcachedDecoder : public Decoder, Logger::Loggable<Logger::Id::redis> {
public:
  MemcachedDecoder(DecoderCallbacks& callbacks) : callbacks_(callbacks) {}

  void decode(Buffer::Instance& data) override ;

private:
  enum class State {
    MC_START,
    MC_SIMPLE_STR,
    MC_CHECK_CMD,
    MC_SINGLE_KEY,
    MC_SINGLE_KEY_END,
    MC_SINGLE_KEY_EXTRA,
    MC_FLAGS,
    MC_FLAGS_END,
    MC_EXPTIME,
    MC_EXPTIME_END,
    MC_BYTES,
    MC_BYTES_END,
    MC_DATA_KEY_EXTRA,
    MC_DATA,
    MC_DATA_SKIP_END,
    MC_DATA_END,
    MC_GAT_EXPIRE_BEGIN,
    MC_GAT_EXPIRE_END,
    MC_MULTI_KEY,
    MC_MULTI_KEY_ONE,
    MC_DONE
  };

  void parseSlice(const Buffer::RawSlice& slice);

  State state_{State::MC_START};
  std::string cmd_;
  uint64_t pending_data_length_;
  DecoderCallbacks& callbacks_;
  RespValuePtr pending_value_root_;
};

/**
 * A factory implementation that returns a real decoder.
 */
class DecoderFactoryImpl : public DecoderFactory {
public:
  // RedisProxy::DecoderFactory
  DecoderPtr create(DecoderCallbacks& callbacks, Protocol protocol) override {
    if (protocol == Protocol::Memcached) {
      return DecoderPtr{new MemcachedDecoder(callbacks) };
    }

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

private:
  void encodeArray(const std::vector<RespValue>& array, Buffer::Instance& out);
  void encodeCompositeArray(const RespValue::CompositeArray& array, Buffer::Instance& out);
  void encodeBulkString(const std::string& string, Buffer::Instance& out);
  void encodeError(const std::string& string, Buffer::Instance& out);
  void encodeInteger(int64_t integer, Buffer::Instance& out);
  void encodeSimpleString(const std::string& string, Buffer::Instance& out);
};

class MemcachedEncoder : public Encoder {
public:
  // RedisProxy::Encoder
  void encode(const RespValue& value, Buffer::Instance& out) override;
};

class EncoderFactoryImpl {
public:
  static EncoderPtr create(Protocol protocol) {
    if (protocol == Protocol::Memcached) {
      return EncoderPtr{ new MemcachedEncoder() };
    }

    return EncoderPtr{ new EncoderImpl() };
  }
};

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
