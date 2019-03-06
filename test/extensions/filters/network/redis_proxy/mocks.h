#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "extensions/filters/network/common/redis/codec_impl.h"
#include "extensions/filters/network/redis_proxy/command_splitter.h"
#include "extensions/filters/network/redis_proxy/conn_pool.h"

#include "test/test_common/printers.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

class MockEncoder : public Common::Redis::Encoder {
public:
  MockEncoder();
  ~MockEncoder();

  MOCK_METHOD2(encode, void(const Common::Redis::RespValue& value, Buffer::Instance& out));

private:
  Common::Redis::EncoderImpl real_encoder_;
};

class MockDecoder : public Common::Redis::Decoder {
public:
  MockDecoder();
  ~MockDecoder();

  MOCK_METHOD1(decode, void(Buffer::Instance& data));
};

namespace ConnPool {

class MockClient : public Client {
public:
  MockClient();
  ~MockClient();

  void raiseEvent(Network::ConnectionEvent event) {
    for (Network::ConnectionCallbacks* callbacks : callbacks_) {
      callbacks->onEvent(event);
    }
  }

  void runHighWatermarkCallbacks() {
    for (auto* callback : callbacks_) {
      callback->onAboveWriteBufferHighWatermark();
    }
  }

  void runLowWatermarkCallbacks() {
    for (auto* callback : callbacks_) {
      callback->onBelowWriteBufferLowWatermark();
    }
  }

  MOCK_METHOD1(addConnectionCallbacks, void(Network::ConnectionCallbacks& callbacks));
  MOCK_METHOD0(close, void());
  MOCK_METHOD2(makeRequest,
               PoolRequest*(const Common::Redis::RespValue& request, PoolCallbacks& callbacks));

  std::list<Network::ConnectionCallbacks*> callbacks_;
};

class MockPoolRequest : public PoolRequest {
public:
  MockPoolRequest();
  ~MockPoolRequest();

  MOCK_METHOD0(cancel, void());
};

class MockPoolCallbacks : public PoolCallbacks {
public:
  MockPoolCallbacks();
  ~MockPoolCallbacks();

  void onResponse(Common::Redis::RespValuePtr&& value) override { onResponse_(value); }

  MOCK_METHOD1(onResponse_, void(Common::Redis::RespValuePtr& value));
  MOCK_METHOD0(onFailure, void());
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance();

  MOCK_METHOD3(makeRequest,
               PoolRequest*(const std::string& hash_key, const Common::Redis::RespValue& request,
                            PoolCallbacks& callbacks));
};

} // namespace ConnPool

namespace CommandSplitter {

class MockSplitRequest : public SplitRequest {
public:
  MockSplitRequest();
  ~MockSplitRequest();

  MOCK_METHOD0(cancel, void());
};

class MockSplitCallbacks : public SplitCallbacks {
public:
  MockSplitCallbacks();
  ~MockSplitCallbacks();

  void onResponse(Common::Redis::RespValuePtr&& value) override { onResponse_(value); }

  MOCK_METHOD1(onResponse_, void(Common::Redis::RespValuePtr& value));
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance();

  SplitRequestPtr makeRequest(const Common::Redis::RespValue& request,
                              SplitCallbacks& callbacks) override {
    return SplitRequestPtr{makeRequest_(request, callbacks)};
  }

  MOCK_METHOD2(makeRequest_,
               SplitRequest*(const Common::Redis::RespValue& request, SplitCallbacks& callbacks));
};

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
