#pragma once

#include <cstdint>
#include <forward_list>
#include <string>
#include <vector>

#include "common/common/logger.h"

#include "extensions/filters/network/common/redis/codec.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

class FragementRespValue : public RespValue {
public:
  FragementRespValue(RespValueSharedPtr base, const RespValue& command, const uint64_t start,
                     const uint64_t end)
      : base_(std::move(base)), command_(command), start_(start), end_(end) {
    type(RespType::Array);
  }

  ~FragementRespValue() override {}

  struct FragementRespValueConstIterator {
    FragementRespValueConstIterator(const RespValue& command, const std::vector<RespValue>& array,
                                    uint64_t index)
        : command_(command), array_(array), index_(index), first_(true) {}
    const RespValue& operator*() { return first_ ? command_ : array_[index_]; }
    FragementRespValueConstIterator operator++() {
      if (first_) {
        first_ = false;
      } else {
        index_++;
      }
      return *this;
    }
    bool operator!=(const FragementRespValueConstIterator& rhs) const {
      return &command_ != &(rhs.command_) || &array_ != &rhs.array_ || index_ != rhs.index_;
    }

    const RespValue& command_;
    const std::vector<RespValue>& array_;
    uint64_t index_;
    bool first_;
  };

  FragementRespValueConstIterator begin() const noexcept {
    return {command_, base_->asArray(), start_};
  }

  FragementRespValueConstIterator end() const noexcept {
    return {command_, base_->asArray(), end_};
  }

  uint64_t size() const { return end_ - start_ + 1; }

private:
  const RespValueSharedPtr base_;
  const RespValue& command_;
  const uint64_t start_;
  const uint64_t end_;
};

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

private:
  void encodeArray(const std::vector<RespValue>& array, Buffer::Instance& out);
  ;
  void encodeCompositeArray(const RespValue::CompositeArray& array, Buffer::Instance& out);
  void encodeBulkString(const std::string& string, Buffer::Instance& out);
  void encodeError(const std::string& string, Buffer::Instance& out);
  void encodeInteger(int64_t integer, Buffer::Instance& out);
  void encodeSimpleString(const std::string& string, Buffer::Instance& out);
};

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
