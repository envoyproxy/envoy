#pragma once

#include "envoy/redis/conn_pool.h"

#include "common/redis/codec_impl.h"

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

  void raiseEvents(uint32_t events) {
    for (Network::ConnectionCallbacks* callbacks : callbacks_) {
      callbacks->onEvent(events);
    }
  }

  MOCK_METHOD1(addConnectionCallbacks, void(Network::ConnectionCallbacks& callbacks));
  MOCK_METHOD0(close, void());
  MOCK_METHOD2(makeRequest,
               ActiveRequest*(const RespValue& request, ActiveRequestCallbacks& callbacks));

  std::list<Network::ConnectionCallbacks*> callbacks_;
};

class MockActiveRequest : public ActiveRequest {
public:
  MockActiveRequest();
  ~MockActiveRequest();

  MOCK_METHOD0(cancel, void());
};

class MockActiveRequestCallbacks : public ActiveRequestCallbacks {
public:
  MockActiveRequestCallbacks();
  ~MockActiveRequestCallbacks();

  void onResponse(RespValuePtr&& value) override { onResponse_(value); }

  MOCK_METHOD1(onResponse_, void(RespValuePtr& value));
  MOCK_METHOD0(onFailure, void());
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance();

  MOCK_METHOD3(makeRequest, ActiveRequest*(const std::string& hash_key, const RespValue& request,
                                           ActiveRequestCallbacks& callbacks));
};

} // ConnPool
} // Redis
