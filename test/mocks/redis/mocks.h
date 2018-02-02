#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "envoy/redis/command_splitter.h"
#include "envoy/redis/conn_pool.h"

#include "common/redis/codec_impl.h"

#include "test/test_common/printers.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Redis {

bool operator==(const RespValue& lhs, const RespValue& rhs);

class MockEncoder : public Encoder {
public:
  MockEncoder();
  ~MockEncoder();

  MOCK_METHOD2(encode, void(const RespValue& value, Buffer::Instance& out));

private:
  EncoderImpl real_encoder_;
};

class MockDecoder : public Decoder {
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
  MOCK_METHOD2(makeRequest, PoolRequest*(const RespValue& request, PoolCallbacks& callbacks));

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

  void onResponse(RespValuePtr&& value) override { onResponse_(value); }

  MOCK_METHOD1(onResponse_, void(RespValuePtr& value));
  MOCK_METHOD0(onFailure, void());
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance();

  MOCK_METHOD3(makeRequest, PoolRequest*(const std::string& hash_key, const RespValue& request,
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

  void onResponse(RespValuePtr&& value) override { onResponse_(value); }

  MOCK_METHOD1(onResponse_, void(RespValuePtr& value));
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance();

  SplitRequestPtr makeRequest(const RespValue& request, SplitCallbacks& callbacks) override {
    return SplitRequestPtr{makeRequest_(request, callbacks)};
  }

  MOCK_METHOD2(makeRequest_, SplitRequest*(const RespValue& request, SplitCallbacks& callbacks));
};

} // namespace CommandSplitter
} // namespace Redis
} // namespace Envoy
