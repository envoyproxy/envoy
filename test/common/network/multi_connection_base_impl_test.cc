#include <cstdint>
#include <memory>

#include "source/common/network/address_impl.h"
#include "source/common/network/multi_connection_base_impl.h"
#include "source/common/network/transport_socket_options_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/network/transport_socket.h"
#include "test/mocks/stream_info/mocks.h"

using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;

namespace Envoy {
namespace Network {

class MockConnectionProvider : public ConnectionProvider {
public:
  MockConnectionProvider(size_t connections) : connections_(connections) {
    for (size_t i = 0; i < connections; i++) {
      next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
    }
  }
  bool hasNextConnection() override { return created_connections_.size() < connections_; }

  MOCK_METHOD(ClientConnectionPtr, createNextConnection, (const uint64_t), (override));

  // Called by MultiConnectionBaseImpl to return a MockClientConnection. In order to allow
  // expectations to be set on this connection, the object must exist. So instead of allocating a
  // new MockClientConnection in this method, it instead pops the first entry from next_connections_
  // and returns that. It also saves a pointer to that connection into created_connections_ so that
  // it can be interacted with after it has been returned.
  ClientConnectionPtr getNextConnection(const uint64_t) {
    auto conn = std::move(next_connections_.front());
    next_connections_.pop_front();
    created_connections_.push_back(conn.get());
    EXPECT_CALL(*created_connections_.back(), addConnectionCallbacks(_))
        .WillOnce(
            Invoke([&](ConnectionCallbacks& cb) -> void { connection_callbacks_.push_back(&cb); }));
    return conn;
  }

  size_t nextConnection() override { return created_connections_.size(); }
  size_t totalConnections() override { return connections_; }

  const std::vector<StrictMock<MockClientConnection>*>& createdConns() const {
    return created_connections_;
  }

  StrictMock<MockClientConnection>* nextConn() { return next_connections_.front().get(); }

  const std::vector<ConnectionCallbacks*>& connCbs() const { return connection_callbacks_; }

private:
  std::vector<StrictMock<MockClientConnection>*> created_connections_;
  std::vector<ConnectionCallbacks*> connection_callbacks_;
  std::deque<std::unique_ptr<StrictMock<MockClientConnection>>> next_connections_;
  const size_t connections_;
};

class MockMultiConnectionImpl : public MultiConnectionBaseImpl {
public:
  MockMultiConnectionImpl(Event::Dispatcher& dispatcher,
                          std::unique_ptr<ConnectionProvider> connection_provider)
      : MultiConnectionBaseImpl(dispatcher, std::move(connection_provider)) {}
};

class MultiConnectionBaseImplTest : public testing::Test {
public:
  MultiConnectionBaseImplTest()
      : failover_timer_(new testing::StrictMock<Event::MockTimer>(&dispatcher_)) {}

  void expectConnectionCreation(size_t id) {
    EXPECT_CALL(*connection_provider_, createNextConnection(_))
        .WillOnce(testing::Invoke([ this, id ](uint64_t conn_id) -> auto{
          EXPECT_EQ(connection_provider_->nextConnection(), id);
          return connection_provider_->getNextConnection(conn_id);
        }));
  }

  void setupMultiConnectionImpl(size_t connections) {
    auto connection_provider = std::make_unique<MockConnectionProvider>(connections);
    connection_provider_ = connection_provider.get();
    expectConnectionCreation(0);
    impl_ = std::make_unique<MockMultiConnectionImpl>(dispatcher_, std::move(connection_provider));
  }

  // Calls connect() on the impl and verifies that the timer is started.
  void startConnect() {
    EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr));
    EXPECT_CALL(*failover_timer_, enabled()).WillRepeatedly(Return(true));
    EXPECT_CALL(*createdConnections()[0], connect());
    impl_->connect();
  }

  // Connects the first (and only attempt).
  void connectFirstAttempt() {
    startConnect();

    EXPECT_CALL(*failover_timer_, disableTimer());
    EXPECT_CALL(*createdConnections()[0], removeConnectionCallbacks(_));
    connectionCallbacks()[0]->onEvent(ConnectionEvent::Connected);
  }

  // Fires the failover timer and creates the next connection.
  void timeOutAndStartNextAttempt() {
    expectConnectionCreation(1);
    EXPECT_CALL(*nextConnection(), connect());
    EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr));
    failover_timer_->invokeCallback();
  }

  // Have the second connection attempt succeed which should disable the fallback timer,
  // and close the first attempt.
  void connectSecondAttempt() {
    ASSERT_EQ(2, createdConnections().size());
    EXPECT_CALL(*failover_timer_, disableTimer());
    EXPECT_CALL(*createdConnections()[1], removeConnectionCallbacks(_));
    EXPECT_CALL(*createdConnections()[0], removeConnectionCallbacks(_));
    EXPECT_CALL(*createdConnections()[0], close(ConnectionCloseType::NoFlush));
    connectionCallbacks()[1]->onEvent(ConnectionEvent::Connected);
  }

  const std::vector<StrictMock<MockClientConnection>*>& createdConnections() const {
    return connection_provider_->createdConns();
  }

  StrictMock<MockClientConnection>* nextConnection() { return connection_provider_->nextConn(); }

  const std::vector<ConnectionCallbacks*>& connectionCallbacks() const {
    return connection_provider_->connCbs();
  }

protected:
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::StrictMock<Event::MockTimer>* failover_timer_;
  MockConnectionProvider* connection_provider_;
  std::unique_ptr<MockMultiConnectionImpl> impl_;
};

TEST_F(MultiConnectionBaseImplTest, Connect) {
  setupMultiConnectionImpl(2);
  startConnect();
}

TEST_F(MultiConnectionBaseImplTest, ConnectTimeout) {
  setupMultiConnectionImpl(3);
  startConnect();

  timeOutAndStartNextAttempt();
  expectConnectionCreation(2);
  EXPECT_CALL(*nextConnection(), connect());
  // Since there are no more addresses to connect to, the fallback timer will not
  // be rescheduled.
  failover_timer_->invokeCallback();
}

TEST_F(MultiConnectionBaseImplTest, ConnectFailed) {
  setupMultiConnectionImpl(3);
  startConnect();

  // When the first connection attempt fails, the next attempt will be immediately
  // started and the timer will be armed for the third attempt.
  expectConnectionCreation(1);
  EXPECT_CALL(*nextConnection(), connect());
  EXPECT_CALL(*createdConnections()[0], removeConnectionCallbacks(_));
  EXPECT_CALL(*createdConnections()[0], close(ConnectionCloseType::NoFlush));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr));
  connectionCallbacks()[0]->onEvent(ConnectionEvent::RemoteClose);
}

TEST_F(MultiConnectionBaseImplTest, ConnectFirstSuccess) {
  setupMultiConnectionImpl(2);
  startConnect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*createdConnections()[0], removeConnectionCallbacks(_));
  connectionCallbacks()[0]->onEvent(ConnectionEvent::Connected);
}

TEST_F(MultiConnectionBaseImplTest, ConnectTimeoutThenFirstSuccess) {
  setupMultiConnectionImpl(3);
  startConnect();

  timeOutAndStartNextAttempt();

  // Connect the first attempt and verify that the second is closed.
  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*createdConnections()[0], removeConnectionCallbacks(_));
  EXPECT_CALL(*createdConnections()[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*createdConnections()[1], close(ConnectionCloseType::NoFlush));
  connectionCallbacks()[0]->onEvent(ConnectionEvent::Connected);

  // Verify that calls are delegated to the right connection.
  EXPECT_CALL(*createdConnections()[0], connecting()).WillOnce(Return(false));
  EXPECT_FALSE(impl_->connecting());
}

TEST_F(MultiConnectionBaseImplTest, DisallowedFunctions) {
  setupMultiConnectionImpl(2);
  startConnect();

  EXPECT_ENVOY_BUG(connectionCallbacks()[0]->onAboveWriteBufferHighWatermark(),
                   "Unexpected data written to MultiConnectionBaseImpl");
  EXPECT_ENVOY_BUG(connectionCallbacks()[0]->onBelowWriteBufferLowWatermark(),
                   "Unexpected data drained from MultiConnectionBaseImpl");
}

TEST_F(MultiConnectionBaseImplTest, ConnectTimeoutThenSecondSuccess) {
  setupMultiConnectionImpl(3);
  startConnect();

  timeOutAndStartNextAttempt();

  // Connect the second attempt and verify that the first is closed.
  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*createdConnections()[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*createdConnections()[0], removeConnectionCallbacks(_));
  EXPECT_CALL(*createdConnections()[0], close(ConnectionCloseType::NoFlush));
  connectionCallbacks()[1]->onEvent(ConnectionEvent::Connected);

  // Verify that calls are delegated to the right connection.
  EXPECT_CALL(*createdConnections()[1], connecting()).WillOnce(Return(false));
  EXPECT_FALSE(impl_->connecting());
}
// TODO(wanlill): should the name be ConnectTimeoutThenSecondFailsAndThirdSucceeds instead?
TEST_F(MultiConnectionBaseImplTest, ConnectTimeoutThenSecondFailsAndFirstSucceeds) {
  setupMultiConnectionImpl(3);
  startConnect();

  timeOutAndStartNextAttempt();

  // When the second attempt fails, the third and final attempt will be started.
  expectConnectionCreation(2);
  EXPECT_CALL(*nextConnection(), connect());
  // Since there are no more address to connect to, the fallback timer will not
  // be rescheduled.
  EXPECT_CALL(*failover_timer_, disableTimer());
  ASSERT_EQ(2, createdConnections().size());

  EXPECT_CALL(*createdConnections()[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*createdConnections()[1], close(ConnectionCloseType::NoFlush));
  connectionCallbacks()[1]->onEvent(ConnectionEvent::RemoteClose);

  EXPECT_CALL(*createdConnections()[0], removeConnectionCallbacks(_));
  EXPECT_CALL(*createdConnections()[0], close(ConnectionCloseType::NoFlush));
  connectionCallbacks()[0]->onEvent(ConnectionEvent::RemoteClose);
}

TEST_F(MultiConnectionBaseImplTest, ConnectThenAllTimeoutAndFail) {
  setupMultiConnectionImpl(3);
  startConnect();

  timeOutAndStartNextAttempt();

  // After the second timeout the third and final attempt will be started.
  expectConnectionCreation(2);
  EXPECT_CALL(*nextConnection(), connect());
  ASSERT_EQ(2, createdConnections().size());
  failover_timer_->invokeCallback();

  EXPECT_CALL(*createdConnections()[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*createdConnections()[1], close(ConnectionCloseType::NoFlush));
  connectionCallbacks()[1]->onEvent(ConnectionEvent::RemoteClose);

  EXPECT_CALL(*createdConnections()[0], removeConnectionCallbacks(_));
  EXPECT_CALL(*createdConnections()[0], close(ConnectionCloseType::NoFlush));
  connectionCallbacks()[0]->onEvent(ConnectionEvent::RemoteClose);

  EXPECT_CALL(*createdConnections()[2], removeConnectionCallbacks(_));
  EXPECT_CALL(*failover_timer_, disableTimer());
  connectionCallbacks()[2]->onEvent(ConnectionEvent::RemoteClose);
}

TEST_F(MultiConnectionBaseImplTest, Id) {
  setupMultiConnectionImpl(2);

  uint64_t id = ConnectionImpl::nextGlobalIdForTest() - 1;
  EXPECT_EQ(id, impl_->id());

  startConnect();

  EXPECT_EQ(id, impl_->id());
}

TEST_F(MultiConnectionBaseImplTest, HashKey) {
  setupMultiConnectionImpl(2);

  uint64_t id = ConnectionImpl::nextGlobalIdForTest() - 1;

  startConnect();

  std::vector<uint8_t> hash_key = {'A', 'B', 'C'};
  uint8_t* id_array = reinterpret_cast<uint8_t*>(&id);
  impl_->hashKey(hash_key);
  EXPECT_EQ(3 + sizeof(id), hash_key.size());
  EXPECT_EQ('A', hash_key[0]);
  EXPECT_EQ('B', hash_key[1]);
  EXPECT_EQ('C', hash_key[2]);
  for (size_t i = 0; i < sizeof(id); ++i) {
    EXPECT_EQ(id_array[i], hash_key[i + 3]);
  }
}

TEST_F(MultiConnectionBaseImplTest, NoDelay) {
  setupMultiConnectionImpl(3);

  EXPECT_CALL(*createdConnections()[0], noDelay(true));
  impl_->noDelay(true);

  startConnect();

  // noDelay() should be applied to the newly created connection.
  EXPECT_CALL(*nextConnection(), noDelay(true));
  timeOutAndStartNextAttempt();

  connectSecondAttempt();

  // Verify that noDelay calls are delegated to the remaining connection.
  EXPECT_CALL(*createdConnections()[1], noDelay(false));
  impl_->noDelay(false);
}

TEST_F(MultiConnectionBaseImplTest, DetectEarlyCloseWhenReadDisabled) {
  setupMultiConnectionImpl(3);

  EXPECT_CALL(*createdConnections()[0], detectEarlyCloseWhenReadDisabled(true));
  impl_->detectEarlyCloseWhenReadDisabled(true);

  startConnect();

  // detectEarlyCloseWhenReadDisabled() should be applied to the newly created connection.
  EXPECT_CALL(*nextConnection(), detectEarlyCloseWhenReadDisabled(true));
  timeOutAndStartNextAttempt();

  connectSecondAttempt();

  // Verify that detectEarlyCloseWhenReadDisabled() calls are delegated to the remaining connection.
  EXPECT_CALL(*createdConnections()[1], detectEarlyCloseWhenReadDisabled(false));
  impl_->detectEarlyCloseWhenReadDisabled(false);
}

TEST_F(MultiConnectionBaseImplTest, SetDelayedCloseTimeout) {
  setupMultiConnectionImpl(3);

  startConnect();

  EXPECT_CALL(*createdConnections()[0], setDelayedCloseTimeout(std::chrono::milliseconds(5)));
  impl_->setDelayedCloseTimeout(std::chrono::milliseconds(5));

  // setDelayedCloseTimeout() should be applied to the newly created connection.
  EXPECT_CALL(*nextConnection(), setDelayedCloseTimeout(std::chrono::milliseconds(5)));
  timeOutAndStartNextAttempt();

  connectSecondAttempt();

  // Verify that setDelayedCloseTimeout() calls are delegated to the remaining connection.
  EXPECT_CALL(*createdConnections()[1], setDelayedCloseTimeout(std::chrono::milliseconds(10)));
  impl_->setDelayedCloseTimeout(std::chrono::milliseconds(10));
}

TEST_F(MultiConnectionBaseImplTest, CloseDuringAttempt) {
  setupMultiConnectionImpl(3);

  startConnect();

  timeOutAndStartNextAttempt();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*createdConnections()[0], removeConnectionCallbacks(_));
  EXPECT_CALL(*createdConnections()[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*createdConnections()[0], close(ConnectionCloseType::FlushWrite));
  EXPECT_CALL(*createdConnections()[1], close(ConnectionCloseType::NoFlush));
  impl_->close(ConnectionCloseType::FlushWrite);
}

TEST_F(MultiConnectionBaseImplTest, CloseDuringAttemptWithCallbacks) {
  setupMultiConnectionImpl(3);
  startConnect();

  MockConnectionCallbacks callbacks;
  // The filter will be captured by the impl and not passed to the connection until it is closed.
  impl_->addConnectionCallbacks(callbacks);

  timeOutAndStartNextAttempt();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*createdConnections()[0], removeConnectionCallbacks(_));
  EXPECT_CALL(*createdConnections()[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*createdConnections()[1], close(ConnectionCloseType::NoFlush));
  // addConnectionCallbacks() should be applied to the now final connection.
  EXPECT_CALL(*createdConnections()[0], addConnectionCallbacks(_))
      .WillOnce(Invoke([&](ConnectionCallbacks& c) -> void { EXPECT_EQ(&c, &callbacks); }));
  EXPECT_CALL(*createdConnections()[0], close(ConnectionCloseType::FlushWrite));
  impl_->close(ConnectionCloseType::FlushWrite);
}

TEST_F(MultiConnectionBaseImplTest, CloseAfterAttemptComplete) {
  setupMultiConnectionImpl(2);

  connectFirstAttempt();

  EXPECT_CALL(*createdConnections()[0], close(ConnectionCloseType::FlushWrite));
  impl_->close(ConnectionCloseType::FlushWrite);
}

TEST_F(MultiConnectionBaseImplTest, AddReadFilter) {
  setupMultiConnectionImpl(3);

  MockReadFilterCallbacks callbacks;
  ReadFilterSharedPtr filter = std::make_shared<MockReadFilter>();
  filter->initializeReadFilterCallbacks(callbacks);
  // The filter will be captured by the impl and not passed to the connection until it completes.
  impl_->addReadFilter(filter);

  startConnect();

  timeOutAndStartNextAttempt();

  // addReadFilter() should be applied to the now final connection.
  EXPECT_CALL(*createdConnections()[1], addReadFilter(filter));
  connectSecondAttempt();

  ReadFilterSharedPtr filter2 = std::make_shared<MockReadFilter>();
  filter2->initializeReadFilterCallbacks(callbacks);
  // Verify that addReadFilter() calls are delegated to the remaining connection.
  EXPECT_CALL(*createdConnections()[1], addReadFilter(filter2));
  impl_->addReadFilter(filter2);
}

TEST_F(MultiConnectionBaseImplTest, RemoveReadFilter) {
  setupMultiConnectionImpl(2);

  startConnect();

  connectFirstAttempt();

  MockReadFilterCallbacks callbacks;
  ReadFilterSharedPtr filter = std::make_shared<MockReadFilter>();
  filter->initializeReadFilterCallbacks(callbacks);
  // Verify that removeReadFilter() calls are delegated to the final connection.
  EXPECT_CALL(*createdConnections()[0], removeReadFilter(filter));
  impl_->removeReadFilter(filter);
}

TEST_F(MultiConnectionBaseImplTest, RemoveReadFilterBeforeConnectFinished) {
  setupMultiConnectionImpl(3);

  startConnect();

  MockReadFilterCallbacks callbacks;
  ReadFilterSharedPtr filter = std::make_shared<MockReadFilter>();
  filter->initializeReadFilterCallbacks(callbacks);
  ReadFilterSharedPtr filter2 = std::make_shared<MockReadFilter>();
  filter2->initializeReadFilterCallbacks(callbacks);
  // The filters will be captured by the impl and not passed to the connection until it completes.
  impl_->addReadFilter(filter);
  impl_->addReadFilter(filter2);

  // The removal will be captured by the impl and not passed to the connection until it completes.
  impl_->removeReadFilter(filter);

  timeOutAndStartNextAttempt();

  // Verify that addReadFilter() calls are delegated to the remaining connection.
  EXPECT_CALL(*createdConnections()[1], addReadFilter(filter2));
  connectSecondAttempt();
}

TEST_F(MultiConnectionBaseImplTest, InitializeReadFilters) {
  setupMultiConnectionImpl(3);

  startConnect();

  // No read filters have been added
  EXPECT_FALSE(impl_->initializeReadFilters());

  MockReadFilterCallbacks callbacks;
  ReadFilterSharedPtr filter = std::make_shared<MockReadFilter>();
  filter->initializeReadFilterCallbacks(callbacks);
  impl_->addReadFilter(filter);

  // initializeReadFilters() will be captured by the impl and not passed to the connection until it
  // completes.
  EXPECT_TRUE(impl_->initializeReadFilters());

  timeOutAndStartNextAttempt();

  // initializeReadFilters() should be applied to the now final connection.
  EXPECT_CALL(*createdConnections()[1], addReadFilter(_));
  EXPECT_CALL(*createdConnections()[1], initializeReadFilters()).WillOnce(Return(true));
  connectSecondAttempt();

  ReadFilterSharedPtr filter2 = std::make_shared<MockReadFilter>();
  filter2->initializeReadFilterCallbacks(callbacks);
  // Verify that addReadFilter() calls are delegated to the remaining connection.
  EXPECT_CALL(*createdConnections()[1], addReadFilter(filter2));
  impl_->addReadFilter(filter2);
}

TEST_F(MultiConnectionBaseImplTest, InitializeReadFiltersAfterConnect) {
  setupMultiConnectionImpl(2);

  startConnect();

  connectFirstAttempt();

  // Verify that initializeReadFilters() calls are delegated to the remaining connection.
  EXPECT_CALL(*createdConnections()[0], initializeReadFilters()).WillOnce(Return(false));
  EXPECT_FALSE(impl_->initializeReadFilters());
}

TEST_F(MultiConnectionBaseImplTest, AddConnectionCallbacks) {
  setupMultiConnectionImpl(3);

  MockConnectionCallbacks callbacks;
  // The filter will be captured by the impl and not passed to the connection until it completes.
  impl_->addConnectionCallbacks(callbacks);

  startConnect();

  timeOutAndStartNextAttempt();

  // addConnectionCallbacks() should be applied to the now final connection.
  EXPECT_CALL(*createdConnections()[1], addConnectionCallbacks(_))
      .WillOnce(Invoke([&](ConnectionCallbacks& c) -> void { EXPECT_EQ(&c, &callbacks); }));
  connectSecondAttempt();

  MockConnectionCallbacks callbacks2;
  // Verify that addConnectionCallbacks() calls are delegated to the remaining connection.
  EXPECT_CALL(*createdConnections()[1], addConnectionCallbacks(_))
      .WillOnce(Invoke([&](ConnectionCallbacks& c) -> void { EXPECT_EQ(&c, &callbacks2); }));
  impl_->addConnectionCallbacks(callbacks2);
}

TEST_F(MultiConnectionBaseImplTest, RemoveConnectionCallbacks) {
  setupMultiConnectionImpl(2);

  MockConnectionCallbacks callbacks;
  MockConnectionCallbacks callbacks2;
  // The callbacks will be captured by the impl and not passed to the connection until it completes.
  impl_->addConnectionCallbacks(callbacks);
  impl_->addConnectionCallbacks(callbacks2);

  startConnect();

  impl_->removeConnectionCallbacks(callbacks);

  // addConnectionCallbacks() should be applied to the now final connection.
  EXPECT_CALL(*createdConnections()[0], addConnectionCallbacks(_))
      .WillOnce(Invoke([&](ConnectionCallbacks& c) -> void { EXPECT_EQ(&c, &callbacks2); }));
  connectFirstAttempt();

  // Verify that removeConnectionCallbacks calls are delegated to the remaining connection.
  EXPECT_CALL(*createdConnections()[0], removeConnectionCallbacks(_))
      .WillOnce(Invoke([&](ConnectionCallbacks& c) -> void { EXPECT_EQ(&c, &callbacks2); }));
  impl_->removeConnectionCallbacks(callbacks2);
}

TEST_F(MultiConnectionBaseImplTest, WriteBeforeConnect) {
  setupMultiConnectionImpl(2);

  Buffer::OwnedImpl data("hello world");
  bool end_stream = false;

  impl_->write(data, end_stream);

  startConnect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*createdConnections()[0], removeConnectionCallbacks(_));
  // The call to write() will be replayed on the underlying connection.
  EXPECT_CALL(*createdConnections()[0], write(_, _))
      .WillOnce(Invoke([](Buffer::Instance& data, bool end_stream) -> void {
        EXPECT_EQ("hello world", data.toString());
        EXPECT_FALSE(end_stream);
        ;
      }));
  connectionCallbacks()[0]->onEvent(ConnectionEvent::Connected);
}

TEST_F(MultiConnectionBaseImplTest, WriteTwiceBeforeConnect) {
  setupMultiConnectionImpl(2);

  Buffer::OwnedImpl data1("hello world");
  Buffer::OwnedImpl data2("goodbye");

  impl_->write(data1, false);
  impl_->write(data2, true);

  startConnect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*createdConnections()[0], removeConnectionCallbacks(_));
  // The call to write() will be replayed on the underlying connection.
  EXPECT_CALL(*createdConnections()[0], write(_, _))
      .WillOnce(Invoke([](Buffer::Instance& data, bool end_stream) -> void {
        EXPECT_EQ("hello worldgoodbye", data.toString());
        EXPECT_TRUE(end_stream);
        ;
      }));
  connectionCallbacks()[0]->onEvent(ConnectionEvent::Connected);
}

TEST_F(MultiConnectionBaseImplTest, Write) {
  setupMultiConnectionImpl(2);

  startConnect();

  connectFirstAttempt();

  Buffer::OwnedImpl data("hello world");
  // The call to write() will be replayed on the underlying connection.
  EXPECT_CALL(*createdConnections()[0], write(_, _))
      .WillOnce(Invoke([](Buffer::Instance& data, bool end_stream) -> void {
        EXPECT_EQ("hello world", data.toString());
        EXPECT_TRUE(end_stream);
        ;
      }));
  impl_->write(data, true);
}

TEST_F(MultiConnectionBaseImplTest, SetBufferLimits) {
  setupMultiConnectionImpl(3);

  startConnect();

  EXPECT_CALL(*createdConnections()[0], setBufferLimits(42));
  impl_->setBufferLimits(42);

  // setBufferLimits() should be applied to the newly created connection.
  EXPECT_CALL(*nextConnection(), setBufferLimits(42));
  timeOutAndStartNextAttempt();

  connectSecondAttempt();

  // Verify that removeConnectionCallbacks calls are delegated to the remaining connection.
  EXPECT_CALL(*createdConnections()[1], setBufferLimits(420));
  impl_->setBufferLimits(420);
}

TEST_F(MultiConnectionBaseImplTest, WriteBeforeLimit) {
  setupMultiConnectionImpl(2);

  startConnect();

  Buffer::OwnedImpl data("hello world");
  size_t length = data.length();
  bool end_stream = false;

  impl_->write(data, end_stream);
  EXPECT_FALSE(impl_->aboveHighWatermark());

  EXPECT_CALL(*createdConnections()[0], setBufferLimits(length - 1));
  impl_->setBufferLimits(length - 1);
  EXPECT_TRUE(impl_->aboveHighWatermark());

  // The call to write() will be replayed on the underlying connection.
  EXPECT_CALL(*createdConnections()[0], write(_, _))
      .WillOnce(Invoke([](Buffer::Instance& data, bool end_stream) -> void {
        EXPECT_EQ("hello world", data.toString());
        EXPECT_FALSE(end_stream);
      }));
  EXPECT_CALL(*createdConnections()[0], bufferLimit()).WillRepeatedly(Return(length - 1));
  connectFirstAttempt();
}

TEST_F(MultiConnectionBaseImplTest, WriteBeforeConnectOverLimit) {
  setupMultiConnectionImpl(2);

  startConnect();

  Buffer::OwnedImpl data("hello world");
  size_t length = data.length();
  bool end_stream = false;

  EXPECT_CALL(*createdConnections()[0], setBufferLimits(length - 1));
  impl_->setBufferLimits(data.length() - 1);

  impl_->write(data, end_stream);

  EXPECT_CALL(*createdConnections()[0], bufferLimit()).WillRepeatedly(Return(length - 1));
  // The call to write() will be replayed on the underlying connection.
  EXPECT_CALL(*createdConnections()[0], write(_, _))
      .WillOnce(Invoke([](Buffer::Instance& data, bool end_stream) -> void {
        EXPECT_EQ("hello world", data.toString());
        EXPECT_FALSE(end_stream);
      }));
  connectFirstAttempt();
}

TEST_F(MultiConnectionBaseImplTest, WriteBeforeConnectOverLimitWithCallbacks) {
  setupMultiConnectionImpl(2);

  startConnect();

  MockConnectionCallbacks callbacks;
  // The filter will be captured by the impl and not passed to the connection until it is closed.
  impl_->addConnectionCallbacks(callbacks);

  Buffer::OwnedImpl data("hello world");
  size_t length = data.length();
  bool end_stream = false;

  EXPECT_CALL(*createdConnections()[0], setBufferLimits(length - 1));
  impl_->setBufferLimits(data.length() - 1);

  EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark());
  impl_->write(data, end_stream);

  {
    // The call to write() will be replayed on the underlying connection, but it will be done
    // after the temporary callbacks are removed and before the final callbacks are added.
    // This causes the underlying connection's high watermark notification to be swallowed.
    testing::InSequence s;
    EXPECT_CALL(*failover_timer_, disableTimer());
    EXPECT_CALL(*createdConnections()[0], removeConnectionCallbacks(_));
    EXPECT_CALL(*createdConnections()[0], bufferLimit()).WillRepeatedly(Return(length - 1));
    EXPECT_CALL(*createdConnections()[0], write(_, _));
    EXPECT_CALL(*createdConnections()[0], addConnectionCallbacks(_));
    connectionCallbacks()[0]->onEvent(ConnectionEvent::Connected);
  }
}

TEST_F(MultiConnectionBaseImplTest, AboveHighWatermark) {
  setupMultiConnectionImpl(2);

  startConnect();

  EXPECT_FALSE(impl_->aboveHighWatermark());

  connectFirstAttempt();

  // Delegates to the connection once connected.
  EXPECT_CALL(*createdConnections()[0], aboveHighWatermark()).WillOnce(Return(true));
  EXPECT_TRUE(impl_->aboveHighWatermark());
}

TEST_F(MultiConnectionBaseImplTest, SetConnectionStats) {
  setupMultiConnectionImpl(3);

  StrictMock<Stats::MockCounter> rx_total;
  StrictMock<Stats::MockGauge> rx_current;
  StrictMock<Stats::MockCounter> tx_total;
  StrictMock<Stats::MockGauge> tx_current;
  StrictMock<Stats::MockCounter> bind_errors;
  StrictMock<Stats::MockCounter> delayed_close_timeouts;

  Connection::ConnectionStats cs = {rx_total,   rx_current,   tx_total,
                                    tx_current, &bind_errors, &delayed_close_timeouts};
  EXPECT_CALL(*createdConnections()[0], setConnectionStats(_))
      .WillOnce(Invoke([&](const Connection::ConnectionStats& s) -> void { EXPECT_EQ(&s, &cs); }));
  impl_->setConnectionStats(cs);

  startConnect();

  // setConnectionStats() should be applied to the newly created connection.
  // Here, it is using stats latched by the multi connection base connection and so
  // will be its own unique data structure.
  EXPECT_CALL(*nextConnection(), setConnectionStats(_))
      .WillOnce(Invoke([&](const Connection::ConnectionStats& s) -> void { EXPECT_NE(&s, &cs); }));
  timeOutAndStartNextAttempt();

  connectSecondAttempt();

  // Verify that setConnectionStats calls are delegated to the remaining connection.
  Connection::ConnectionStats cs2 = {rx_total,   rx_current,   tx_total,
                                     tx_current, &bind_errors, &delayed_close_timeouts};
  EXPECT_CALL(*createdConnections()[1], setConnectionStats(_))
      .WillOnce(Invoke([&](const Connection::ConnectionStats& s) -> void { EXPECT_EQ(&s, &cs2); }));
  impl_->setConnectionStats(cs2);
}

TEST_F(MultiConnectionBaseImplTest, State) {
  setupMultiConnectionImpl(2);

  startConnect();

  EXPECT_CALL(*createdConnections()[0], state()).WillRepeatedly(Return(Connection::State::Open));
  EXPECT_EQ(Connection::State::Open, impl_->state());

  connectFirstAttempt();

  EXPECT_CALL(*createdConnections()[0], state()).WillOnce(Return(Connection::State::Closing));
  EXPECT_EQ(Connection::State::Closing, impl_->state());
}

TEST_F(MultiConnectionBaseImplTest, Connecting) {
  setupMultiConnectionImpl(2);

  startConnect();

  EXPECT_CALL(*createdConnections()[0], connecting()).WillRepeatedly(Return(true));
  EXPECT_TRUE(impl_->connecting());

  connectFirstAttempt();

  EXPECT_CALL(*createdConnections()[0], connecting()).WillRepeatedly(Return(false));
  EXPECT_FALSE(impl_->connecting());
}

TEST_F(MultiConnectionBaseImplTest, AddWriteFilter) {
  setupMultiConnectionImpl(3);

  MockWriteFilterCallbacks callbacks;
  WriteFilterSharedPtr filter = std::make_shared<MockWriteFilter>();
  filter->initializeWriteFilterCallbacks(callbacks);
  // The filter will be captured by the impl and not passed to the connection until it completes.
  impl_->addWriteFilter(filter);

  startConnect();

  timeOutAndStartNextAttempt();

  // addWriteFilter() should be applied to the now final connection.
  EXPECT_CALL(*createdConnections()[1], addWriteFilter(filter));
  connectSecondAttempt();

  WriteFilterSharedPtr filter2 = std::make_shared<MockWriteFilter>();
  filter2->initializeWriteFilterCallbacks(callbacks);
  // Verify that addWriteFilter() calls are delegated to the remaining connection.
  EXPECT_CALL(*createdConnections()[1], addWriteFilter(filter2));
  impl_->addWriteFilter(filter2);
}

TEST_F(MultiConnectionBaseImplTest, AddWriteFilterAfterConnect) {
  setupMultiConnectionImpl(2);

  connectFirstAttempt();

  MockWriteFilterCallbacks callbacks;
  WriteFilterSharedPtr filter = std::make_shared<MockWriteFilter>();
  filter->initializeWriteFilterCallbacks(callbacks);
  EXPECT_CALL(*createdConnections()[0], addWriteFilter(filter));
  impl_->addWriteFilter(filter);
}

TEST_F(MultiConnectionBaseImplTest, AddFilter) {
  setupMultiConnectionImpl(3);

  MockReadFilterCallbacks read_callbacks;
  MockWriteFilterCallbacks write_callbacks;
  FilterSharedPtr filter = std::make_shared<MockFilter>();
  filter->initializeReadFilterCallbacks(read_callbacks);
  filter->initializeWriteFilterCallbacks(write_callbacks);
  // The filter will be captured by the impl and not passed to the connection until it completes.
  impl_->addFilter(filter);

  startConnect();

  timeOutAndStartNextAttempt();

  // addFilter() should be applied to the now final connection.
  EXPECT_CALL(*createdConnections()[1], addFilter(filter));
  connectSecondAttempt();

  FilterSharedPtr filter2 = std::make_shared<MockFilter>();
  filter2->initializeReadFilterCallbacks(read_callbacks);
  filter2->initializeWriteFilterCallbacks(write_callbacks);
  // Verify that addFilter() calls are delegated to the remaining connection.
  EXPECT_CALL(*createdConnections()[1], addFilter(filter2));
  impl_->addFilter(filter2);
}

TEST_F(MultiConnectionBaseImplTest, AddFilterAfterConnect) {
  setupMultiConnectionImpl(2);

  connectFirstAttempt();

  MockReadFilterCallbacks read_callbacks;
  MockWriteFilterCallbacks write_callbacks;
  FilterSharedPtr filter = std::make_shared<MockFilter>();
  filter->initializeReadFilterCallbacks(read_callbacks);
  filter->initializeWriteFilterCallbacks(write_callbacks);
  EXPECT_CALL(*createdConnections()[0], addFilter(filter));
  impl_->addFilter(filter);
}

TEST_F(MultiConnectionBaseImplTest, AddBytesSentCallback) {
  setupMultiConnectionImpl(3);

  Connection::BytesSentCb callback = [](uint64_t) { return true; };
  // The filter will be captured by the impl and not passed to the connection until it completes.
  impl_->addBytesSentCallback(callback);

  startConnect();

  timeOutAndStartNextAttempt();

  // addBytesSentCallback() should be applied to the now final connection.
  EXPECT_CALL(*createdConnections()[1], addBytesSentCallback(_));
  connectSecondAttempt();

  Connection::BytesSentCb callback2 = [](uint64_t) { return true; };
  // Verify that addBytesSentCallback() calls are delegated to the remaining connection.
  EXPECT_CALL(*createdConnections()[1], addBytesSentCallback(_));
  impl_->addBytesSentCallback(callback2);
}

TEST_F(MultiConnectionBaseImplTest, AddBytesSentCallbackAfterConnect) {
  setupMultiConnectionImpl(2);

  connectFirstAttempt();

  Connection::BytesSentCb cb = [](uint64_t) { return true; };
  EXPECT_CALL(*createdConnections()[0], addBytesSentCallback(_));
  impl_->addBytesSentCallback(cb);
}

TEST_F(MultiConnectionBaseImplTest, EnableHalfClose) {
  setupMultiConnectionImpl(3);

  EXPECT_CALL(*createdConnections()[0], enableHalfClose(true));
  impl_->enableHalfClose(true);

  startConnect();

  // enableHalfClose() should be applied to the newly created connection.
  EXPECT_CALL(*nextConnection(), enableHalfClose(true));
  timeOutAndStartNextAttempt();

  connectSecondAttempt();

  // Verify that enableHalfClose calls are delegated to the remaining connection.
  EXPECT_CALL(*createdConnections()[1], enableHalfClose(false));
  impl_->enableHalfClose(false);
}

TEST_F(MultiConnectionBaseImplTest, EnableHalfCloseAfterConnect) {
  setupMultiConnectionImpl(2);

  connectFirstAttempt();

  EXPECT_CALL(*createdConnections()[0], enableHalfClose(true));
  impl_->enableHalfClose(true);
}

TEST_F(MultiConnectionBaseImplTest, IsHalfCloseEnabled) {
  setupMultiConnectionImpl(2);

  EXPECT_CALL(*createdConnections()[0], isHalfCloseEnabled()).WillOnce(Return(false));
  EXPECT_FALSE(impl_->isHalfCloseEnabled());

  EXPECT_CALL(*createdConnections()[0], isHalfCloseEnabled()).WillOnce(Return(true));
  EXPECT_TRUE(impl_->isHalfCloseEnabled());

  connectFirstAttempt();
}

TEST_F(MultiConnectionBaseImplTest, IsHalfCloseEnabledAfterConnect) {
  setupMultiConnectionImpl(2);

  connectFirstAttempt();

  EXPECT_CALL(*createdConnections()[0], isHalfCloseEnabled()).WillOnce(Return(true));
  EXPECT_TRUE(impl_->isHalfCloseEnabled());
}

TEST_F(MultiConnectionBaseImplTest, ReadDisable) {
  setupMultiConnectionImpl(3);

  // The disables will be captured by the impl and not passed to the connection until it completes.
  impl_->readDisable(true);

  startConnect();

  timeOutAndStartNextAttempt();

  // The disables will be captured by the impl and not passed to the connection until it completes.
  impl_->readDisable(true);
  impl_->readDisable(true);
  // Read disable count should now be 2.
  impl_->readDisable(false);

  // readDisable() should be applied to the now final connection.
  EXPECT_CALL(*createdConnections()[1], readDisable(true)).Times(2);
  connectSecondAttempt();

  // Verify that addBytesSentCallback() calls are delegated to the remaining connection.
  EXPECT_CALL(*createdConnections()[1], readDisable(false));
  impl_->readDisable(false);
}

TEST_F(MultiConnectionBaseImplTest, ReadEnabled) {
  setupMultiConnectionImpl(2);

  EXPECT_TRUE(impl_->readEnabled());
  impl_->readDisable(true); // Disable count 1.
  EXPECT_FALSE(impl_->readEnabled());

  startConnect();

  impl_->readDisable(true); // Disable count 2
  EXPECT_FALSE(impl_->readEnabled());
  impl_->readDisable(false); // Disable count 1
  EXPECT_FALSE(impl_->readEnabled());
  impl_->readDisable(false); // Disable count 0
  EXPECT_TRUE(impl_->readEnabled());
}

TEST_F(MultiConnectionBaseImplTest, ReadEnabledAfterConnect) {
  setupMultiConnectionImpl(2);

  connectFirstAttempt();

  EXPECT_CALL(*createdConnections()[0], readEnabled()).WillOnce(Return(true));
  EXPECT_TRUE(impl_->readEnabled());
}

TEST_F(MultiConnectionBaseImplTest, StartSecureTransport) {
  setupMultiConnectionImpl(3);

  EXPECT_CALL(*createdConnections()[0], startSecureTransport()).WillOnce(Return(true));
  EXPECT_TRUE(impl_->startSecureTransport());

  startConnect();

  // startSecureTransport() should be applied to the newly created connection.
  EXPECT_CALL(*nextConnection(), startSecureTransport()).WillOnce(Return(true));
  timeOutAndStartNextAttempt();

  EXPECT_CALL(*createdConnections()[0], startSecureTransport()).WillOnce(Return(false));
  EXPECT_CALL(*createdConnections()[1], startSecureTransport()).WillOnce(Return(true));
  EXPECT_FALSE(impl_->startSecureTransport());

  connectSecondAttempt();

  // Verify that startSecureTransport calls are delegated to the remaining connection.
  EXPECT_CALL(*createdConnections()[1], startSecureTransport()).WillOnce(Return(false));
  EXPECT_FALSE(impl_->startSecureTransport());
}

TEST_F(MultiConnectionBaseImplTest, StartSecureTransportAfterConnect) {
  setupMultiConnectionImpl(2);

  connectFirstAttempt();

  EXPECT_CALL(*createdConnections()[0], startSecureTransport());
  impl_->startSecureTransport();
}

// Tests for MultiConnectionBaseImpl methods which simply delegate to the first connection.

TEST_F(MultiConnectionBaseImplTest, Dispatcher) {
  setupMultiConnectionImpl(2);

  connectFirstAttempt();

  EXPECT_CALL(*createdConnections()[0], dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
  EXPECT_EQ(&dispatcher_, &(impl_->dispatcher()));
}

TEST_F(MultiConnectionBaseImplTest, BufferLimit) {
  setupMultiConnectionImpl(2);

  connectFirstAttempt();

  EXPECT_CALL(*createdConnections()[0], bufferLimit()).WillOnce(Return(42));
  EXPECT_EQ(42, impl_->bufferLimit());
}

TEST_F(MultiConnectionBaseImplTest, NextProtocol) {
  setupMultiConnectionImpl(2);

  connectFirstAttempt();

  EXPECT_CALL(*createdConnections()[0], nextProtocol()).WillOnce(Return("h3"));
  EXPECT_EQ("h3", impl_->nextProtocol());
}

TEST_F(MultiConnectionBaseImplTest, AddressProvider) {
  setupMultiConnectionImpl(2);

  connectFirstAttempt();

  ConnectionInfoSetterImpl provider(std::make_shared<Address::Ipv4Instance>(80),
                                    std::make_shared<Address::Ipv4Instance>(80));
  EXPECT_CALL(*createdConnections()[0], connectionInfoProvider()).WillOnce(ReturnRef(provider));
  impl_->connectionInfoProvider();
}

TEST_F(MultiConnectionBaseImplTest, AddressProviderSharedPtr) {
  setupMultiConnectionImpl(2);

  connectFirstAttempt();

  ConnectionInfoProviderSharedPtr provider = std::make_shared<ConnectionInfoSetterImpl>(
      std::make_shared<Address::Ipv4Instance>("127.0.0.2"),
      std::make_shared<Address::Ipv4Instance>("127.0.0.1"));
  EXPECT_CALL(*createdConnections()[0], connectionInfoProviderSharedPtr())
      .WillOnce(Return(provider));
  EXPECT_EQ(provider, impl_->connectionInfoProviderSharedPtr());
}

TEST_F(MultiConnectionBaseImplTest, UnixSocketPeerCredentials) {
  setupMultiConnectionImpl(2);

  connectFirstAttempt();

  EXPECT_CALL(*createdConnections()[0], unixSocketPeerCredentials())
      .WillOnce(Return(absl::optional<Connection::UnixDomainSocketPeerCredentials>()));
  EXPECT_FALSE(impl_->unixSocketPeerCredentials().has_value());
}

TEST_F(MultiConnectionBaseImplTest, Ssl) {
  setupMultiConnectionImpl(2);

  connectFirstAttempt();

  Ssl::ConnectionInfoConstSharedPtr ssl = nullptr;
  EXPECT_CALL(*createdConnections()[0], ssl()).WillOnce(Return(ssl));
  EXPECT_EQ(ssl, impl_->ssl());
}

TEST_F(MultiConnectionBaseImplTest, SocketOptions) {
  setupMultiConnectionImpl(2);

  connectFirstAttempt();

  ConnectionSocket::OptionsSharedPtr options = nullptr;
  EXPECT_CALL(*createdConnections()[0], socketOptions()).WillOnce(ReturnRef(options));
  EXPECT_EQ(options, impl_->socketOptions());
}

TEST_F(MultiConnectionBaseImplTest, RequestedServerName) {
  setupMultiConnectionImpl(2);

  connectFirstAttempt();

  EXPECT_CALL(*createdConnections()[0], requestedServerName()).WillOnce(Return("name"));
  EXPECT_EQ("name", impl_->requestedServerName());
}

TEST_F(MultiConnectionBaseImplTest, StreamInfo) {
  setupMultiConnectionImpl(2);

  connectFirstAttempt();

  StreamInfo::MockStreamInfo info;
  EXPECT_CALL(*createdConnections()[0], streamInfo()).WillOnce(ReturnRef(info));
  EXPECT_EQ(&info, &impl_->streamInfo());
}

TEST_F(MultiConnectionBaseImplTest, ConstStreamInfo) {
  setupMultiConnectionImpl(2);

  connectFirstAttempt();

  StreamInfo::MockStreamInfo info;
  EXPECT_CALL(*createdConnections()[0], streamInfo()).WillOnce(ReturnRef(info));
  const MockMultiConnectionImpl* impl = impl_.get();
  EXPECT_EQ(&info, &impl->streamInfo());
}

TEST_F(MultiConnectionBaseImplTest, TransportFailureReason) {
  setupMultiConnectionImpl(2);

  connectFirstAttempt();

  EXPECT_CALL(*createdConnections()[0], transportFailureReason()).WillOnce(Return("reason"));
  EXPECT_EQ("reason", impl_->transportFailureReason());
}

TEST_F(MultiConnectionBaseImplTest, LastRoundTripTime) {
  setupMultiConnectionImpl(2);

  connectFirstAttempt();

  absl::optional<std::chrono::milliseconds> rtt = std::chrono::milliseconds(5);
  EXPECT_CALL(*createdConnections()[0], lastRoundTripTime()).WillOnce(Return(rtt));
  EXPECT_EQ(rtt, impl_->lastRoundTripTime());
}

} // namespace Network
} // namespace Envoy
