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

class MockCancellable : public Cancellable {
public:
  MockCancellable();
  ~MockCancellable();

  // Tcp::ConnectionPool::Cancellable
  MOCK_METHOD0(cancel, void());
};

class MockUpstreamCallbacks : public UpstreamCallbacks {
public:
  MockUpstreamCallbacks();
  ~MockUpstreamCallbacks();

  // Tcp::ConnectionPool::UpstreamCallbacks
  MOCK_METHOD2(onUpstreamData, void(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(onEvent, void(Network::ConnectionEvent event));
  MOCK_METHOD0(onAboveWriteBufferHighWatermark, void());
  MOCK_METHOD0(onBelowWriteBufferLowWatermark, void());
};

class MockConnectionData : public ConnectionData {
public:
  MockConnectionData();
  ~MockConnectionData();

  // Tcp::ConnectionPool::ConnectionData
  MOCK_METHOD0(connection, Network::ClientConnection&());
  MOCK_METHOD1(addUpstreamCallbacks, void(ConnectionPool::UpstreamCallbacks&));
  void setConnectionState(ConnectionStatePtr&& state) override { setConnectionState_(state); }
  MOCK_METHOD0(connectionState, ConnectionPool::ConnectionState*());

  MOCK_METHOD1(setConnectionState_, void(ConnectionPool::ConnectionStatePtr& state));

  // If set, invoked in ~MockConnectionData, which indicates that the connection pool
  // caller has relased a connection.
  std::function<void()> release_callback_;
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance();

  // Tcp::ConnectionPool::Instance
  MOCK_METHOD1(addDrainedCallback, void(DrainedCb cb));
  MOCK_METHOD0(drainConnections, void());
  MOCK_METHOD1(newConnection, Cancellable*(Tcp::ConnectionPool::Callbacks& callbacks));

  MockCancellable* newConnectionImpl(Callbacks& cb);
  void poolFailure(PoolFailureReason reason);
  void poolReady(Network::MockClientConnection& conn);

  // Invoked when connection_data_, having been assigned via poolReady is released.
  MOCK_METHOD1(released, void(Network::MockClientConnection&));

  std::list<NiceMock<MockCancellable>> handles_;
  std::list<Callbacks*> callbacks_;

  std::shared_ptr<NiceMock<Upstream::MockHostDescription>> host_{
      new NiceMock<Upstream::MockHostDescription>()};
  std::unique_ptr<NiceMock<MockConnectionData>> connection_data_{
      new NiceMock<MockConnectionData>()};
};

} // namespace ConnectionPool
} // namespace Tcp
} // namespace Envoy
