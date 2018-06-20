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
};

class MockConnectionData : public ConnectionData {
public:
  MockConnectionData();
  ~MockConnectionData();

  // Tcp::ConnectionPool::ConnectionData
  MOCK_METHOD0(connection, Network::ClientConnection&());
  MOCK_METHOD1(addUpstreamCallbacks, void(ConnectionPool::UpstreamCallbacks&));
  MOCK_METHOD0(release, void());

  NiceMock<Network::MockClientConnection> connection_;
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance();

  // Tcp::ConnectionPool::Instance
  MOCK_METHOD1(addDrainedCallback, void(DrainedCb cb));
  MOCK_METHOD0(drainConnections, void());
  MOCK_METHOD1(newConnection, Cancellable*(Tcp::ConnectionPool::Callbacks& callbacks));

  std::shared_ptr<testing::NiceMock<Upstream::MockHostDescription>> host_{
      new testing::NiceMock<Upstream::MockHostDescription>()};
};

} // namespace ConnectionPool
} // namespace Tcp
} // namespace Envoy
