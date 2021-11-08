#pragma once

#include "common/buffer/buffer_impl.h"
#include "extensions/filters/network/brpc_proxy/codec.h"


namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace BrpcProxy {
//todo inherit Logger::Loggable<Logger::Id::brpc>
class DecoderImpl : public Decoder, public Logger::Loggable<Logger::Id::brpc> {
public:
  DecoderImpl(DecoderCallbacks& callbacks) : callbacks_(callbacks) {}

  // RedisProxy::Decoder
  void decode(Buffer::Instance& data) override;

private:
  enum class State {
  	Init,
	Head,
	Meta,
	Body,
	Complete
  };


  void parseSlice(const Buffer::RawSlice& slice);
  DecoderCallbacks& callbacks_;
  BrpcMessagePtr msgptr_; 
  State state_{State::Init};
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
  void encode(BrpcMessage& value, Buffer::Instance& out) override;
};

} // namespace BrpcProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

