#pragma once

#include "gmock/gmock.h"

#include "test/mocks/network/connection.h"
#include "source/extensions/health_checkers/thrift/client.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace ThriftHealthChecker {

class MockClient : public Client {
public:
  MockClient(ClientCallback& callback);
  ~MockClient() override;

  void raiseEvent(Network::ConnectionEvent event) {
    callback_.onEvent(event);
  }

  void raiseResponseResult(bool is_success) {
    callback_.onResponseResult(is_success);
  }

  void runHighWatermarkCallbacks() {
    callback_.onAboveWriteBufferHighWatermark();
  }

  void runLowWatermarkCallbacks() {
    callback_.onBelowWriteBufferLowWatermark();
  }

  MOCK_METHOD(void, start, ());
  MOCK_METHOD(bool, makeRequest, ());
  MOCK_METHOD(void, close, ());

private:
  ClientCallback& callback_;
};

class MockClientCallback : public ClientCallback , public Network::MockConnectionCallbacks{
public:
  MockClientCallback();
  ~MockClientCallback() override;

  MOCK_METHOD(void, onResponseResult, (bool));
  MOCK_METHOD(Upstream::Host::CreateConnectionData, createConnection, ());
};

} // namespace ThriftHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
