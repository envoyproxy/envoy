#pragma once

#include <cstdint>
#include <forward_list>
#include <string>
#include <vector>
#include <iostream>

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
  std::string backend_;
};

/**
 * MemcachedDecoder mc decode
 *          decoder
 * client ----------->        --------------> upstream
 *                   \ proxy /
 * client <----------/       \<-------------- upstream
 *          decoder
 */
class MemcachedDecoder : public Decoder, Logger::Loggable<Logger::Id::redis> {
  public:
    MemcachedDecoder(DecoderCallbacks& callbacks) : callbacks_(callbacks) {}

    void decode(Buffer::Instance& data) override ;
  private:

    enum class MsgType {
      MSG_UNKNOWN,
      MSG_REQ_MC_SET,
      MSG_REQ_MC_CAS,
      MSG_REQ_MC_ADD,
      MSG_REQ_MC_REPLACE,
      MSG_REQ_MC_APPEND,
      MSG_REQ_MC_PREPEND,
      MSG_REQ_MC_GET,
      MSG_REQ_MC_GETS,
      MSG_REQ_MC_INCR,
      MSG_REQ_MC_DECR,
      MSG_REQ_MC_DELETE,
      MSG_REQ_MC_TOUCH,

      MSG_RSP_MC_NUM,
      MSG_RSP_MC_STORED,
      MSG_RSP_MC_NOT_STORED,
      MSG_RSP_MC_EXISTS,
      MSG_RSP_MC_NOT_FOUND,
      MSG_RSP_MC_END,
      MSG_RSP_MC_VALUE,
      MSG_RSP_MC_DELETED,
      MSG_RSP_MC_TOUCHED,
      MSG_RSP_MC_ERROR,
      MSG_RSP_MC_CLIENT_ERROR,
      MSG_RSP_MC_SERVER_ERROR
    };

    enum class State {
      SW_START,

      SW_REQ_CMD,
      
      SW_KEY,
      SW_FLAGS,
      SW_EXPIRY,
      SW_VLEN,
      SW_CAS,
      SW_VAL,
      SW_NOREPLY,
      SW_CRLF,

      SW_RSP_NUM,
      SW_RSP_STR,
      SW_RSP_ERR_MSG,
      SW_RSP_CAS,
      SW_RSP_END,

      SW_DONE
    };

    void parseSlice(const Buffer::RawSlice& slice);

    State state_{State::SW_START};
    DecoderCallbacks& callbacks_;
    RespValuePtr pending_value_root_;
    MsgType type_{MsgType::MSG_UNKNOWN};
  };

/**
 * A factory implementation that returns a real decoder.
 */
class DecoderFactoryImpl : public DecoderFactory {
public:
  // RedisProxy::DecoderFactory
  DecoderPtr create(DecoderCallbacks& callbacks) override {
//    if (callbacks.backends() == "memcached") {
//      return DecoderPtr{new MemcachedDecoder(callbacks) };
//    }
    // TODO ???
    // std::cout << "backends: " << callbacks.backends() << std::endl;
    return DecoderPtr{new MemcachedDecoder(callbacks) };

    // return DecoderPtr{new RedisDecoder(callbacks)};
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

private:
  void encodeRequest(const std::string& cmd, const std::vector<RespValue>& array, Buffer::Instance& out);
  void encodeResponse(const std::string& resp, const std::vector<RespValue>& array, Buffer::Instance& out);
  void encode_storage(const std::vector<RespValue>& array, Buffer::Instance& out);
  void encode_cas(const std::vector<RespValue>& array, Buffer::Instance& out);
  void encode_get_delete(const std::vector<RespValue>& array, Buffer::Instance& out);
  void encode_arithmetic(const std::vector<RespValue>& array, Buffer::Instance& out);
  void encode_touch(const std::vector<RespValue>& array, Buffer::Instance& out);
  void encode_result(const std::vector<RespValue>& array, Buffer::Instance& out);
  void encode_error(const std::vector<RespValue>& array, Buffer::Instance& out);
  void encode_value(const std::vector<RespValue>& array, Buffer::Instance& out);
};

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
