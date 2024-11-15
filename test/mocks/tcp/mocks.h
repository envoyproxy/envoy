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

class MockCallbacks : public Callbacks {
  MOCK_METHOD(void, onPoolFailure,
              (PoolFailureReason reason, absl::string_view details,
               Upstream::HostDescriptionConstSharedPtr host));
  MOCK_METHOD(void, onPoolReady,
              (ConnectionDataPtr && conn, Upstream::HostDescriptionConstSharedPtr host));
};

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
  MOCK_METHOD(void, addIdleCallback, (IdleCb cb));
  MOCK_METHOD(bool, isIdle, (), (const));
  MOCK_METHOD(void, drainConnections, (Envoy::ConnectionPool::DrainBehavior drain_behavior));
  MOCK_METHOD(void, closeConnections, ());
  MOCK_METHOD(Cancellable*, newConnection, (Tcp::ConnectionPool::Callbacks & callbacks));
  MOCK_METHOD(bool, maybePreconnect, (float), ());
  MOCK_METHOD(Upstream::HostDescriptionConstSharedPtr, host, (), (const));

  Envoy::ConnectionPool::MockCancellable* newConnectionImpl(Callbacks& cb);
  void poolFailure(PoolFailureReason reason, bool host_null = false);
  void poolReady(Network::MockClientConnection& conn);

  // Invoked when connection_data_, having been assigned via poolReady is released.
  MOCK_METHOD(void, released, (Network::MockClientConnection&));

  std::list<NiceMock<Envoy::ConnectionPool::MockCancellable>> handles_;
  std::list<Callbacks*> callbacks_;
  IdleCb idle_cb_;

  std::shared_ptr<NiceMock<Upstream::MockHostDescription>> host_{
      new NiceMock<Upstream::MockHostDescription>()};
  std::unique_ptr<NiceMock<MockConnectionData>> connection_data_{
      new NiceMock<MockConnectionData>()};
};

} // namespace ConnectionPool

namespace AsyncClient {

class MockAsyncTcpClientCallbacks : public AsyncTcpClientCallbacks {
public:
  MockAsyncTcpClientCallbacks() = default;
  ~MockAsyncTcpClientCallbacks() override = default;

  MOCK_METHOD(void, onData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(void, onEvent, (Network::ConnectionEvent event));
  MOCK_METHOD(void, onAboveWriteBufferHighWatermark, ());
  MOCK_METHOD(void, onBelowWriteBufferLowWatermark, ());
};

class MockAsyncTcpClient : public AsyncTcpClient {
public:
  MockAsyncTcpClient() = default;
  ~MockAsyncTcpClient() override = default;

  MOCK_METHOD(bool, connect, ());
  MOCK_METHOD(void, close, (Network::ConnectionCloseType type));
  MOCK_METHOD(Network::DetectedCloseType, detectedCloseType, (), (const));
  MOCK_METHOD(void, write, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(void, readDisable, (bool disable));
  MOCK_METHOD(void, setAsyncTcpClientCallbacks, (AsyncTcpClientCallbacks & callbacks));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(bool, connected, ());
  MOCK_METHOD(OptRef<StreamInfo::StreamInfo>, getStreamInfo, ());
};

} // namespace AsyncClient
} // namespace Tcp
} // namespace Envoy
