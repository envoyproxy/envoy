#pragma once

#include "source/extensions/health_checkers/thrift/client.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace ThriftHealthChecker {

class MockClient : public Client {
public:
  MockClient(ClientCallback& callback);
  ~MockClient() override;

  void raiseEvent(Network::ConnectionEvent event) { callback_.onEvent(event); }

  void raiseResponseResult(bool is_success) { callback_.onResponseResult(is_success); }

  void runHighWatermarkCallbacks() { callback_.onAboveWriteBufferHighWatermark(); }

  void runLowWatermarkCallbacks() { callback_.onBelowWriteBufferLowWatermark(); }

  MOCK_METHOD(void, start, ());
  MOCK_METHOD(bool, sendRequest, ());
  MOCK_METHOD(void, close, ());

private:
  ClientCallback& callback_;
};

class MockClientCallback : public ClientCallback {
public:
  MockClientCallback();
  ~MockClientCallback() override;

  // EXPECT_CALL can not return mock object which needs an implicit cast.
  Upstream::Host::CreateConnectionData createConnection() override {
    Upstream::MockHost::MockCreateConnectionData data = createConnection_();
    return {Network::ClientConnectionPtr{data.connection_}, data.host_description_};
  }

  MOCK_METHOD(void, onResponseResult, (bool));
  MOCK_METHOD(Upstream::MockHost::MockCreateConnectionData, createConnection_, ());

  // Network::ConnectionCallbacks
  MOCK_METHOD(void, onEvent, (Network::ConnectionEvent event));
  MOCK_METHOD(void, onAboveWriteBufferHighWatermark, ());
  MOCK_METHOD(void, onBelowWriteBufferLowWatermark, ());
};

} // namespace ThriftHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
