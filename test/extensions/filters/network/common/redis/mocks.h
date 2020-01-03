#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "extensions/filters/network/common/redis/client_impl.h"
#include "extensions/filters/network/common/redis/codec_impl.h"

#include "test/test_common/printers.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

/**
 * Pretty print const RespValue& value
 */

void PrintTo(const RespValue& value, std::ostream* os);
void PrintTo(const RespValuePtr& value, std::ostream* os);

class MockEncoder : public Common::Redis::Encoder {
public:
  MockEncoder();
  ~MockEncoder() override;

  MOCK_METHOD2(encode, void(const Common::Redis::RespValue& value, Buffer::Instance& out));

private:
  Common::Redis::EncoderImpl real_encoder_;
};

class MockDecoder : public Common::Redis::Decoder {
public:
  MockDecoder();
  ~MockDecoder() override;

  MOCK_METHOD1(decode, void(Buffer::Instance& data));
};

namespace Client {

class MockPoolRequest : public PoolRequest {
public:
  MockPoolRequest();
  ~MockPoolRequest() override;

  MOCK_METHOD0(cancel, void());
};

class MockClient : public Client {
public:
  MockClient();
  ~MockClient() override;

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

  PoolRequest* makeRequest(const Common::Redis::RespValue& request,
                           ClientCallbacks& callbacks) override {
    client_callbacks_.push_back(&callbacks);
    return makeRequest_(request, callbacks);
  }

  MOCK_METHOD1(addConnectionCallbacks, void(Network::ConnectionCallbacks& callbacks));
  MOCK_METHOD0(active, bool());
  MOCK_METHOD0(close, void());
  MOCK_METHOD2(makeRequest_,
               PoolRequest*(const Common::Redis::RespValue& request, ClientCallbacks& callbacks));
  MOCK_METHOD1(initialize, void(const std::string& password));

  std::list<Network::ConnectionCallbacks*> callbacks_;
  std::list<ClientCallbacks*> client_callbacks_;
};

class MockClientCallbacks : public ClientCallbacks {
public:
  MockClientCallbacks();
  ~MockClientCallbacks() override;

  void onResponse(Common::Redis::RespValuePtr&& value) override { onResponse_(value); }
  bool onRedirection(Common::Redis::RespValuePtr&& value, const std::string& host_address,
                     bool ask_redirection) override {
    return onRedirection_(value, host_address, ask_redirection);
  }

  MOCK_METHOD1(onResponse_, void(Common::Redis::RespValuePtr& value));
  MOCK_METHOD0(onFailure, void());
  MOCK_METHOD3(onRedirection_, bool(Common::Redis::RespValuePtr& value,
                                    const std::string& host_address, bool ask_redirection));
};

} // namespace Client

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
