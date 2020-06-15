#pragma once

#include "envoy/tcp/conn_pool.h"

#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"

using testing::NiceMock;

namespace Envoy {
namespace Tcp {
namespace ConnectionPool {

class MockUpstreamCallbacks : public UpstreamCallbacks {
public:
  MockUpstreamCallbacks();
  ~MockUpstreamCallbacks() override;

  // Tcp::ConnectionPool::UpstreamCallbacks
  MOCK_METHOD(void, onUpstreamData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(void, onEvent, (Network::ConnectionEvent event));
  MOCK_METHOD(void, onAboveWriteBufferHighWatermark, ());
  MOCK_METHOD(void, onBelowWriteBufferLowWatermark, ());
};

class MockConnectionData : public ConnectionData {
public:
  MockConnectionData();
  ~MockConnectionData() override;

  // Tcp::ConnectionPool::ConnectionData
  MOCK_METHOD(Network::ClientConnection&, connection, ());
  MOCK_METHOD(void, addUpstreamCallbacks, (ConnectionPool::UpstreamCallbacks&));
  void setConnectionState(ConnectionStatePtr&& state) override { setConnectionState_(state); }
  MOCK_METHOD(ConnectionPool::ConnectionState*, connectionState, ());

  MOCK_METHOD(void, setConnectionState_, (ConnectionPool::ConnectionStatePtr & state));

  // If set, invoked in ~MockConnectionData, which indicates that the connection pool
  // caller has released a connection.
  std::function<void()> release_callback_;
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance() override;

  // Tcp::ConnectionPool::Instance
  MOCK_METHOD(void, addDrainedCallback, (DrainedCb cb));
  MOCK_METHOD(void, drainConnections, ());
  MOCK_METHOD(void, closeConnections, ());
  MOCK_METHOD(Cancellable*, newConnection, (Tcp::ConnectionPool::Callbacks & callbacks));
  MOCK_METHOD(Upstream::HostDescriptionConstSharedPtr, host, (), (const));

  Envoy::ConnectionPool::MockCancellable* newConnectionImpl(Callbacks& cb);
  void poolFailure(PoolFailureReason reason);
  void poolReady(Network::MockClientConnection& conn);

  // Invoked when connection_data_, having been assigned via poolReady is released.
  MOCK_METHOD(void, released, (Network::MockClientConnection&));

  std::list<NiceMock<Envoy::ConnectionPool::MockCancellable>> handles_;
  std::list<Callbacks*> callbacks_;

  std::shared_ptr<NiceMock<Upstream::MockHostDescription>> host_{
      new NiceMock<Upstream::MockHostDescription>()};
  std::unique_ptr<NiceMock<MockConnectionData>> connection_data_{
      new NiceMock<MockConnectionData>()};
};

} // namespace ConnectionPool
} // namespace Tcp
} // namespace Envoy
