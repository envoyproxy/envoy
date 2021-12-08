#include "source/common/network/address_impl.h"
#include "source/common/network/happy_eyeballs_connection_impl.h"
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

class HappyEyeballsConnectionImplTest : public testing::Test {
public:
  HappyEyeballsConnectionImplTest()
      : failover_timer_(new testing::StrictMock<Event::MockTimer>(&dispatcher_)),
        transport_socket_options_(std::make_shared<TransportSocketOptionsImpl>()),
        options_(std::make_shared<ConnectionSocket::Options>()),
        raw_address_list_({std::make_shared<Address::Ipv4Instance>("127.0.0.1"),
                           std::make_shared<Address::Ipv4Instance>("127.0.0.2"),
                           std::make_shared<Address::Ipv6Instance>("ff02::1", 0)}),
        address_list_({raw_address_list_[0], raw_address_list_[2], raw_address_list_[1]}) {
    EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
    EXPECT_CALL(dispatcher_, createClientConnection_(address_list_[0], _, _, _))
        .WillOnce(testing::InvokeWithoutArgs(
            this, &HappyEyeballsConnectionImplTest::createNextConnection));

    next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
    impl_ = std::make_unique<HappyEyeballsConnectionImpl>(
        dispatcher_, raw_address_list_, Address::InstanceConstSharedPtr(),
        transport_socket_factory_, transport_socket_options_, options_);
  }

  // Called by the dispatcher to return a MockClientConnection. In order to allow expectations to
  // be set on this connection, the object must exist. So instead of allocating a new
  // MockClientConnection in this method, it instead pops the first entry from
  // next_connections_ and returns that. It also saves a pointer to that connection into
  // created_connections_ so that it can be interacted with after it has been returned.
  MockClientConnection* createNextConnection() {
    created_connections_.push_back(next_connections_.front().release());
    next_connections_.pop_front();
    EXPECT_CALL(*created_connections_.back(), addConnectionCallbacks(_))
        .WillOnce(
            Invoke([&](ConnectionCallbacks& cb) -> void { connection_callbacks_.push_back(&cb); }));
    return created_connections_.back();
  }

  // Calls connect() on the impl and verifies that the timer is started.
  void startConnect() {
    EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr));
    EXPECT_CALL(*failover_timer_, enabled()).WillRepeatedly(Return(true));
    EXPECT_CALL(*created_connections_[0], connect());
    impl_->connect();
  }

  // Connects the first (and only attempt).
  void connectFirstAttempt() {
    startConnect();

    EXPECT_CALL(*failover_timer_, disableTimer());
    EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
    connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);
  }

  // Fires the failover timer and creates the next connection.
  void timeOutAndStartNextAttempt() {
    EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
    EXPECT_CALL(dispatcher_, createClientConnection_(address_list_[1], _, _, _))
        .WillOnce(testing::InvokeWithoutArgs(
            this, &HappyEyeballsConnectionImplTest::createNextConnection));
    EXPECT_CALL(*next_connections_.back(), connect());
    EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr));
    failover_timer_->invokeCallback();
  }

  // Have the second connection attempt succeed which should disable the fallback timer,
  // and close the first attempt.
  void connectSecondAttempt() {
    ASSERT_EQ(2, created_connections_.size());
    EXPECT_CALL(*failover_timer_, disableTimer());
    EXPECT_CALL(*created_connections_[1], removeConnectionCallbacks(_));
    EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
    EXPECT_CALL(*created_connections_[0], close(ConnectionCloseType::NoFlush));
    connection_callbacks_[1]->onEvent(ConnectionEvent::Connected);
  }

protected:
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::StrictMock<Event::MockTimer>* failover_timer_;
  MockTransportSocketFactory transport_socket_factory_;
  TransportSocketOptionsConstSharedPtr transport_socket_options_;
  const ConnectionSocket::OptionsSharedPtr options_;
  const std::vector<Address::InstanceConstSharedPtr> raw_address_list_;
  const std::vector<Address::InstanceConstSharedPtr> address_list_;
  std::vector<StrictMock<MockClientConnection>*> created_connections_;
  std::vector<ConnectionCallbacks*> connection_callbacks_;
  std::deque<std::unique_ptr<StrictMock<MockClientConnection>>> next_connections_;
  std::unique_ptr<HappyEyeballsConnectionImpl> impl_;
};

TEST_F(HappyEyeballsConnectionImplTest, Connect) { startConnect(); }

TEST_F(HappyEyeballsConnectionImplTest, ConnectTimeout) {
  startConnect();

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  timeOutAndStartNextAttempt();

  // Let the second attempt timeout to start the third and final attempt.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(address_list_[2], _, _, _))
      .WillOnce(
          testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  // Since there are no more addresses to connect to, the fallback timer will not
  // be rescheduled.
  failover_timer_->invokeCallback();
}

TEST_F(HappyEyeballsConnectionImplTest, ConnectFailed) {
  startConnect();

  // When the first connection attempt fails, the next attempt will be immediately
  // started and the timer will be armed for the third attempt.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _))
      .WillOnce(
          testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[0], close(ConnectionCloseType::NoFlush));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr));
  connection_callbacks_[0]->onEvent(ConnectionEvent::RemoteClose);
}

TEST_F(HappyEyeballsConnectionImplTest, ConnectFirstSuccess) {
  startConnect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);
}

TEST_F(HappyEyeballsConnectionImplTest, ConnectTimeoutThenFirstSuccess) {
  startConnect();

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  timeOutAndStartNextAttempt();

  // Connect the first attempt and verify that the second is closed.
  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[1], close(ConnectionCloseType::NoFlush));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);

  // Verify that calls are delegated to the right connection.
  EXPECT_CALL(*created_connections_[0], connecting()).WillOnce(Return(false));
  EXPECT_FALSE(impl_->connecting());
}

TEST_F(HappyEyeballsConnectionImplTest, ConnectTimeoutThenSecondSuccess) {
  startConnect();

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  timeOutAndStartNextAttempt();

  // Connect the second attempt and verify that the first is closed.
  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[0], close(ConnectionCloseType::NoFlush));
  connection_callbacks_[1]->onEvent(ConnectionEvent::Connected);

  // Verify that calls are delegated to the right connection.
  EXPECT_CALL(*created_connections_[1], connecting()).WillOnce(Return(false));
  EXPECT_FALSE(impl_->connecting());
}

TEST_F(HappyEyeballsConnectionImplTest, ConnectTimeoutThenSecondFailsAndFirstSucceeds) {
  startConnect();

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  timeOutAndStartNextAttempt();

  // When the second attempt fails, the third and final attempt will be started.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(address_list_[2], _, _, _))
      .WillOnce(
          testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  // Since there are no more address to connect to, the fallback timer will not
  // be rescheduled.
  EXPECT_CALL(*failover_timer_, disableTimer());
  ASSERT_EQ(2, created_connections_.size());

  EXPECT_CALL(*created_connections_[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[1], close(ConnectionCloseType::NoFlush));
  connection_callbacks_[1]->onEvent(ConnectionEvent::RemoteClose);

  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[0], close(ConnectionCloseType::NoFlush));
  connection_callbacks_[0]->onEvent(ConnectionEvent::RemoteClose);
}

TEST_F(HappyEyeballsConnectionImplTest, ConnectThenAllTimeoutAndFail) {
  startConnect();

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  timeOutAndStartNextAttempt();

  // After the second timeout the third and final attempt will be started.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(address_list_[2], _, _, _))
      .WillOnce(
          testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  ASSERT_EQ(2, created_connections_.size());
  failover_timer_->invokeCallback();

  EXPECT_CALL(*created_connections_[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[1], close(ConnectionCloseType::NoFlush));
  connection_callbacks_[1]->onEvent(ConnectionEvent::RemoteClose);

  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[0], close(ConnectionCloseType::NoFlush));
  connection_callbacks_[0]->onEvent(ConnectionEvent::RemoteClose);

  EXPECT_CALL(*created_connections_[2], removeConnectionCallbacks(_));
  EXPECT_CALL(*failover_timer_, disableTimer());
  connection_callbacks_[2]->onEvent(ConnectionEvent::RemoteClose);
}

TEST_F(HappyEyeballsConnectionImplTest, Id) {
  uint64_t id = ConnectionImpl::nextGlobalIdForTest() - 1;
  EXPECT_EQ(id, impl_->id());

  startConnect();

  EXPECT_EQ(id, impl_->id());
}

TEST_F(HappyEyeballsConnectionImplTest, HashKey) {
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

TEST_F(HappyEyeballsConnectionImplTest, NoDelay) {
  EXPECT_CALL(*created_connections_[0], noDelay(true));
  impl_->noDelay(true);

  startConnect();

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  // noDelay() should be applied to the newly created connection.
  EXPECT_CALL(*next_connections_.back(), noDelay(true));
  timeOutAndStartNextAttempt();

  connectSecondAttempt();

  // Verify that noDelay calls are delegated to the remaining connection.
  EXPECT_CALL(*created_connections_[1], noDelay(false));
  impl_->noDelay(false);
}

TEST_F(HappyEyeballsConnectionImplTest, DetectEarlyCloseWhenReadDisabled) {
  EXPECT_CALL(*created_connections_[0], detectEarlyCloseWhenReadDisabled(true));
  impl_->detectEarlyCloseWhenReadDisabled(true);

  startConnect();

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  // detectEarlyCloseWhenReadDisabled() should be applied to the newly created connection.
  EXPECT_CALL(*next_connections_.back(), detectEarlyCloseWhenReadDisabled(true));
  timeOutAndStartNextAttempt();

  connectSecondAttempt();

  // Verify that detectEarlyCloseWhenReadDisabled() calls are delegated to the remaining connection.
  EXPECT_CALL(*created_connections_[1], detectEarlyCloseWhenReadDisabled(false));
  impl_->detectEarlyCloseWhenReadDisabled(false);
}

TEST_F(HappyEyeballsConnectionImplTest, SetDelayedCloseTimeout) {
  startConnect();

  EXPECT_CALL(*created_connections_[0], setDelayedCloseTimeout(std::chrono::milliseconds(5)));
  impl_->setDelayedCloseTimeout(std::chrono::milliseconds(5));

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  // setDelayedCloseTimeout() should be applied to the newly created connection.
  EXPECT_CALL(*next_connections_.back(), setDelayedCloseTimeout(std::chrono::milliseconds(5)));
  timeOutAndStartNextAttempt();

  connectSecondAttempt();

  // Verify that setDelayedCloseTimeout() calls are delegated to the remaining connection.
  EXPECT_CALL(*created_connections_[1], setDelayedCloseTimeout(std::chrono::milliseconds(10)));
  impl_->setDelayedCloseTimeout(std::chrono::milliseconds(10));
}

TEST_F(HappyEyeballsConnectionImplTest, CloseDuringAttempt) {
  startConnect();

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  timeOutAndStartNextAttempt();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[0], close(ConnectionCloseType::FlushWrite));
  EXPECT_CALL(*created_connections_[1], close(ConnectionCloseType::NoFlush));
  impl_->close(ConnectionCloseType::FlushWrite);
}

TEST_F(HappyEyeballsConnectionImplTest, CloseDuringAttemptWithCallbacks) {
  startConnect();

  MockConnectionCallbacks callbacks;
  // The filter will be captured by the impl and not passed to the connection until it is closed.
  impl_->addConnectionCallbacks(callbacks);

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  timeOutAndStartNextAttempt();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[1], close(ConnectionCloseType::NoFlush));
  // addConnectionCallbacks() should be applied to the now final connection.
  EXPECT_CALL(*created_connections_[0], addConnectionCallbacks(_))
      .WillOnce(Invoke([&](ConnectionCallbacks& c) -> void { EXPECT_EQ(&c, &callbacks); }));
  EXPECT_CALL(*created_connections_[0], close(ConnectionCloseType::FlushWrite));
  impl_->close(ConnectionCloseType::FlushWrite);
}

TEST_F(HappyEyeballsConnectionImplTest, CloseAfterAttemptComplete) {
  connectFirstAttempt();

  EXPECT_CALL(*created_connections_[0], close(ConnectionCloseType::FlushWrite));
  impl_->close(ConnectionCloseType::FlushWrite);
}

TEST_F(HappyEyeballsConnectionImplTest, AddReadFilter) {
  MockReadFilterCallbacks callbacks;
  ReadFilterSharedPtr filter = std::make_shared<MockReadFilter>();
  filter->initializeReadFilterCallbacks(callbacks);
  // The filter will be captured by the impl and not passed to the connection until it completes.
  impl_->addReadFilter(filter);

  startConnect();

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  timeOutAndStartNextAttempt();

  // addReadFilter() should be applied to the now final connection.
  EXPECT_CALL(*created_connections_[1], addReadFilter(filter));
  connectSecondAttempt();

  ReadFilterSharedPtr filter2 = std::make_shared<MockReadFilter>();
  filter2->initializeReadFilterCallbacks(callbacks);
  // Verify that addReadFilter() calls are delegated to the remaining connection.
  EXPECT_CALL(*created_connections_[1], addReadFilter(filter2));
  impl_->addReadFilter(filter2);
}

TEST_F(HappyEyeballsConnectionImplTest, RemoveReadFilter) {
  startConnect();

  connectFirstAttempt();

  MockReadFilterCallbacks callbacks;
  ReadFilterSharedPtr filter = std::make_shared<MockReadFilter>();
  filter->initializeReadFilterCallbacks(callbacks);
  // Verify that removeReadFilter() calls are delegated to the final connection.
  EXPECT_CALL(*created_connections_[0], removeReadFilter(filter));
  impl_->removeReadFilter(filter);
}

TEST_F(HappyEyeballsConnectionImplTest, RemoveReadFilterBeforeConnectFinished) {
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

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  timeOutAndStartNextAttempt();

  // Verify that addReadFilter() calls are delegated to the remaining connection.
  EXPECT_CALL(*created_connections_[1], addReadFilter(filter2));
  connectSecondAttempt();
}

TEST_F(HappyEyeballsConnectionImplTest, InitializeReadFilters) {
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

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  timeOutAndStartNextAttempt();

  // initializeReadFilters() should be applied to the now final connection.
  EXPECT_CALL(*created_connections_[1], addReadFilter(_));
  EXPECT_CALL(*created_connections_[1], initializeReadFilters()).WillOnce(Return(true));
  connectSecondAttempt();

  ReadFilterSharedPtr filter2 = std::make_shared<MockReadFilter>();
  filter2->initializeReadFilterCallbacks(callbacks);
  // Verify that addReadFilter() calls are delegated to the remaining connection.
  EXPECT_CALL(*created_connections_[1], addReadFilter(filter2));
  impl_->addReadFilter(filter2);
}

TEST_F(HappyEyeballsConnectionImplTest, InitializeReadFiltersAfterConnect) {
  startConnect();

  connectFirstAttempt();

  // Verify that initializeReadFilters() calls are delegated to the remaining connection.
  EXPECT_CALL(*created_connections_[0], initializeReadFilters()).WillOnce(Return(false));
  EXPECT_FALSE(impl_->initializeReadFilters());
}

TEST_F(HappyEyeballsConnectionImplTest, AddConnectionCallbacks) {
  MockConnectionCallbacks callbacks;
  // The filter will be captured by the impl and not passed to the connection until it completes.
  impl_->addConnectionCallbacks(callbacks);

  startConnect();

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  timeOutAndStartNextAttempt();

  // addConnectionCallbacks() should be applied to the now final connection.
  EXPECT_CALL(*created_connections_[1], addConnectionCallbacks(_))
      .WillOnce(Invoke([&](ConnectionCallbacks& c) -> void { EXPECT_EQ(&c, &callbacks); }));
  connectSecondAttempt();

  MockConnectionCallbacks callbacks2;
  // Verify that addConnectionCallbacks() calls are delegated to the remaining connection.
  EXPECT_CALL(*created_connections_[1], addConnectionCallbacks(_))
      .WillOnce(Invoke([&](ConnectionCallbacks& c) -> void { EXPECT_EQ(&c, &callbacks2); }));
  impl_->addConnectionCallbacks(callbacks2);
}

TEST_F(HappyEyeballsConnectionImplTest, RemoveConnectionCallbacks) {
  MockConnectionCallbacks callbacks;
  MockConnectionCallbacks callbacks2;
  // The callbacks will be captured by the impl and not passed to the connection until it completes.
  impl_->addConnectionCallbacks(callbacks);
  impl_->addConnectionCallbacks(callbacks2);

  startConnect();

  impl_->removeConnectionCallbacks(callbacks);

  // addConnectionCallbacks() should be applied to the now final connection.
  EXPECT_CALL(*created_connections_[0], addConnectionCallbacks(_))
      .WillOnce(Invoke([&](ConnectionCallbacks& c) -> void { EXPECT_EQ(&c, &callbacks2); }));
  connectFirstAttempt();

  // Verify that removeConnectionCallbacks calls are delegated to the remaining connection.
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_))
      .WillOnce(Invoke([&](ConnectionCallbacks& c) -> void { EXPECT_EQ(&c, &callbacks2); }));
  impl_->removeConnectionCallbacks(callbacks2);
}

TEST_F(HappyEyeballsConnectionImplTest, WriteBeforeConnect) {
  Buffer::OwnedImpl data("hello world");
  bool end_stream = false;

  impl_->write(data, end_stream);

  startConnect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  // The call to write() will be replayed on the underlying connection.
  EXPECT_CALL(*created_connections_[0], write(_, _))
      .WillOnce(Invoke([](Buffer::Instance& data, bool end_stream) -> void {
        EXPECT_EQ("hello world", data.toString());
        EXPECT_FALSE(end_stream);
        ;
      }));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);
}

TEST_F(HappyEyeballsConnectionImplTest, WriteTwiceBeforeConnect) {
  Buffer::OwnedImpl data1("hello world");
  Buffer::OwnedImpl data2("goodbye");

  impl_->write(data1, false);
  impl_->write(data2, true);

  startConnect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  // The call to write() will be replayed on the underlying connection.
  EXPECT_CALL(*created_connections_[0], write(_, _))
      .WillOnce(Invoke([](Buffer::Instance& data, bool end_stream) -> void {
        EXPECT_EQ("hello worldgoodbye", data.toString());
        EXPECT_TRUE(end_stream);
        ;
      }));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);
}

TEST_F(HappyEyeballsConnectionImplTest, Write) {
  startConnect();

  connectFirstAttempt();

  Buffer::OwnedImpl data("hello world");
  // The call to write() will be replayed on the underlying connection.
  EXPECT_CALL(*created_connections_[0], write(_, _))
      .WillOnce(Invoke([](Buffer::Instance& data, bool end_stream) -> void {
        EXPECT_EQ("hello world", data.toString());
        EXPECT_TRUE(end_stream);
        ;
      }));
  impl_->write(data, true);
}

TEST_F(HappyEyeballsConnectionImplTest, SetBufferLimits) {
  startConnect();

  EXPECT_CALL(*created_connections_[0], setBufferLimits(42));
  impl_->setBufferLimits(42);

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  // setBufferLimits() should be applied to the newly created connection.
  EXPECT_CALL(*next_connections_.back(), setBufferLimits(42));
  timeOutAndStartNextAttempt();

  connectSecondAttempt();

  // Verify that removeConnectionCallbacks calls are delegated to the remaining connection.
  EXPECT_CALL(*created_connections_[1], setBufferLimits(420));
  impl_->setBufferLimits(420);
}

TEST_F(HappyEyeballsConnectionImplTest, WriteBeforeLimit) {
  startConnect();

  Buffer::OwnedImpl data("hello world");
  size_t length = data.length();
  bool end_stream = false;

  impl_->write(data, end_stream);
  EXPECT_FALSE(impl_->aboveHighWatermark());

  EXPECT_CALL(*created_connections_[0], setBufferLimits(length - 1));
  impl_->setBufferLimits(length - 1);
  EXPECT_TRUE(impl_->aboveHighWatermark());

  // The call to write() will be replayed on the underlying connection.
  EXPECT_CALL(*created_connections_[0], write(_, _))
      .WillOnce(Invoke([](Buffer::Instance& data, bool end_stream) -> void {
        EXPECT_EQ("hello world", data.toString());
        EXPECT_FALSE(end_stream);
      }));
  EXPECT_CALL(*created_connections_[0], bufferLimit()).WillRepeatedly(Return(length - 1));
  connectFirstAttempt();
}

TEST_F(HappyEyeballsConnectionImplTest, WriteBeforeConnectOverLimit) {
  startConnect();

  Buffer::OwnedImpl data("hello world");
  size_t length = data.length();
  bool end_stream = false;

  EXPECT_CALL(*created_connections_[0], setBufferLimits(length - 1));
  impl_->setBufferLimits(data.length() - 1);

  impl_->write(data, end_stream);

  EXPECT_CALL(*created_connections_[0], bufferLimit()).WillRepeatedly(Return(length - 1));
  // The call to write() will be replayed on the underlying connection.
  EXPECT_CALL(*created_connections_[0], write(_, _))
      .WillOnce(Invoke([](Buffer::Instance& data, bool end_stream) -> void {
        EXPECT_EQ("hello world", data.toString());
        EXPECT_FALSE(end_stream);
      }));
  connectFirstAttempt();
}

TEST_F(HappyEyeballsConnectionImplTest, WriteBeforeConnectOverLimitWithCallbacks) {
  startConnect();

  MockConnectionCallbacks callbacks;
  // The filter will be captured by the impl and not passed to the connection until it is closed.
  impl_->addConnectionCallbacks(callbacks);

  Buffer::OwnedImpl data("hello world");
  size_t length = data.length();
  bool end_stream = false;

  EXPECT_CALL(*created_connections_[0], setBufferLimits(length - 1));
  impl_->setBufferLimits(data.length() - 1);

  EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark());
  impl_->write(data, end_stream);

  {
    // The call to write() will be replayed on the underlying connection, but it will be done
    // after the temporary callbacks are removed and before the final callbacks are added.
    // This causes the underlying connection's high watermark notification to be swallowed.
    testing::InSequence s;
    EXPECT_CALL(*failover_timer_, disableTimer());
    EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
    EXPECT_CALL(*created_connections_[0], bufferLimit()).WillRepeatedly(Return(length - 1));
    EXPECT_CALL(*created_connections_[0], write(_, _));
    EXPECT_CALL(*created_connections_[0], addConnectionCallbacks(_));
    connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);
  }
}

TEST_F(HappyEyeballsConnectionImplTest, AboveHighWatermark) {
  startConnect();

  EXPECT_FALSE(impl_->aboveHighWatermark());

  connectFirstAttempt();

  // Delegates to the connection once connected.
  EXPECT_CALL(*created_connections_[0], aboveHighWatermark()).WillOnce(Return(true));
  EXPECT_TRUE(impl_->aboveHighWatermark());
}

TEST_F(HappyEyeballsConnectionImplTest, SetConnectionStats) {
  StrictMock<Stats::MockCounter> rx_total;
  StrictMock<Stats::MockGauge> rx_current;
  StrictMock<Stats::MockCounter> tx_total;
  StrictMock<Stats::MockGauge> tx_current;
  StrictMock<Stats::MockCounter> bind_errors;
  StrictMock<Stats::MockCounter> delayed_close_timeouts;

  Connection::ConnectionStats cs = {rx_total,   rx_current,   tx_total,
                                    tx_current, &bind_errors, &delayed_close_timeouts};
  EXPECT_CALL(*created_connections_[0], setConnectionStats(_))
      .WillOnce(Invoke([&](const Connection::ConnectionStats& s) -> void { EXPECT_EQ(&s, &cs); }));
  impl_->setConnectionStats(cs);

  startConnect();

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  // setConnectionStats() should be applied to the newly created connection.
  // Here, it is using stats latched by the happy eyeballs connection and so
  // will be its own unique data structure.
  EXPECT_CALL(*next_connections_.back(), setConnectionStats(_))
      .WillOnce(Invoke([&](const Connection::ConnectionStats& s) -> void { EXPECT_NE(&s, &cs); }));
  timeOutAndStartNextAttempt();

  connectSecondAttempt();

  // Verify that setConnectionStats calls are delegated to the remaining connection.
  Connection::ConnectionStats cs2 = {rx_total,   rx_current,   tx_total,
                                     tx_current, &bind_errors, &delayed_close_timeouts};
  EXPECT_CALL(*created_connections_[1], setConnectionStats(_))
      .WillOnce(Invoke([&](const Connection::ConnectionStats& s) -> void { EXPECT_EQ(&s, &cs2); }));
  impl_->setConnectionStats(cs2);
}

TEST_F(HappyEyeballsConnectionImplTest, State) {
  startConnect();

  EXPECT_CALL(*created_connections_[0], state()).WillRepeatedly(Return(Connection::State::Open));
  EXPECT_EQ(Connection::State::Open, impl_->state());

  connectFirstAttempt();

  EXPECT_CALL(*created_connections_[0], state()).WillOnce(Return(Connection::State::Closing));
  EXPECT_EQ(Connection::State::Closing, impl_->state());
}

TEST_F(HappyEyeballsConnectionImplTest, Connecting) {
  startConnect();

  EXPECT_CALL(*created_connections_[0], connecting()).WillRepeatedly(Return(true));
  EXPECT_TRUE(impl_->connecting());

  connectFirstAttempt();

  EXPECT_CALL(*created_connections_[0], connecting()).WillRepeatedly(Return(false));
  EXPECT_FALSE(impl_->connecting());
}

TEST_F(HappyEyeballsConnectionImplTest, AddWriteFilter) {
  MockWriteFilterCallbacks callbacks;
  WriteFilterSharedPtr filter = std::make_shared<MockWriteFilter>();
  filter->initializeWriteFilterCallbacks(callbacks);
  // The filter will be captured by the impl and not passed to the connection until it completes.
  impl_->addWriteFilter(filter);

  startConnect();

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  timeOutAndStartNextAttempt();

  // addWriteFilter() should be applied to the now final connection.
  EXPECT_CALL(*created_connections_[1], addWriteFilter(filter));
  connectSecondAttempt();

  WriteFilterSharedPtr filter2 = std::make_shared<MockWriteFilter>();
  filter2->initializeWriteFilterCallbacks(callbacks);
  // Verify that addWriteFilter() calls are delegated to the remaining connection.
  EXPECT_CALL(*created_connections_[1], addWriteFilter(filter2));
  impl_->addWriteFilter(filter2);
}

TEST_F(HappyEyeballsConnectionImplTest, AddWriteFilterAfterConnect) {
  connectFirstAttempt();

  MockWriteFilterCallbacks callbacks;
  WriteFilterSharedPtr filter = std::make_shared<MockWriteFilter>();
  filter->initializeWriteFilterCallbacks(callbacks);
  EXPECT_CALL(*created_connections_[0], addWriteFilter(filter));
  impl_->addWriteFilter(filter);
}

TEST_F(HappyEyeballsConnectionImplTest, AddFilter) {
  MockReadFilterCallbacks read_callbacks;
  MockWriteFilterCallbacks write_callbacks;
  FilterSharedPtr filter = std::make_shared<MockFilter>();
  filter->initializeReadFilterCallbacks(read_callbacks);
  filter->initializeWriteFilterCallbacks(write_callbacks);
  // The filter will be captured by the impl and not passed to the connection until it completes.
  impl_->addFilter(filter);

  startConnect();

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  timeOutAndStartNextAttempt();

  // addFilter() should be applied to the now final connection.
  EXPECT_CALL(*created_connections_[1], addFilter(filter));
  connectSecondAttempt();

  FilterSharedPtr filter2 = std::make_shared<MockFilter>();
  filter2->initializeReadFilterCallbacks(read_callbacks);
  filter2->initializeWriteFilterCallbacks(write_callbacks);
  // Verify that addFilter() calls are delegated to the remaining connection.
  EXPECT_CALL(*created_connections_[1], addFilter(filter2));
  impl_->addFilter(filter2);
}

TEST_F(HappyEyeballsConnectionImplTest, AddFilterAfterConnect) {
  connectFirstAttempt();

  MockReadFilterCallbacks read_callbacks;
  MockWriteFilterCallbacks write_callbacks;
  FilterSharedPtr filter = std::make_shared<MockFilter>();
  filter->initializeReadFilterCallbacks(read_callbacks);
  filter->initializeWriteFilterCallbacks(write_callbacks);
  EXPECT_CALL(*created_connections_[0], addFilter(filter));
  impl_->addFilter(filter);
}

TEST_F(HappyEyeballsConnectionImplTest, AddBytesSentCallback) {
  Connection::BytesSentCb callback = [](uint64_t) { return true; };
  // The filter will be captured by the impl and not passed to the connection until it completes.
  impl_->addBytesSentCallback(callback);

  startConnect();

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  timeOutAndStartNextAttempt();

  // addBytesSentCallback() should be applied to the now final connection.
  EXPECT_CALL(*created_connections_[1], addBytesSentCallback(_));
  connectSecondAttempt();

  Connection::BytesSentCb callback2 = [](uint64_t) { return true; };
  // Verify that addBytesSentCallback() calls are delegated to the remaining connection.
  EXPECT_CALL(*created_connections_[1], addBytesSentCallback(_));
  impl_->addBytesSentCallback(callback2);
}

TEST_F(HappyEyeballsConnectionImplTest, AddBytesSentCallbackAfterConnect) {
  connectFirstAttempt();

  Connection::BytesSentCb cb = [](uint64_t) { return true; };
  EXPECT_CALL(*created_connections_[0], addBytesSentCallback(_));
  impl_->addBytesSentCallback(cb);
}

TEST_F(HappyEyeballsConnectionImplTest, EnableHalfClose) {
  EXPECT_CALL(*created_connections_[0], enableHalfClose(true));
  impl_->enableHalfClose(true);

  startConnect();

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  // enableHalfClose() should be applied to the newly created connection.
  EXPECT_CALL(*next_connections_.back(), enableHalfClose(true));
  timeOutAndStartNextAttempt();

  connectSecondAttempt();

  // Verify that enableHalfClose calls are delegated to the remaining connection.
  EXPECT_CALL(*created_connections_[1], enableHalfClose(false));
  impl_->enableHalfClose(false);
}

TEST_F(HappyEyeballsConnectionImplTest, EnableHalfCloseAfterConnect) {
  connectFirstAttempt();

  EXPECT_CALL(*created_connections_[0], enableHalfClose(true));
  impl_->enableHalfClose(true);
}

TEST_F(HappyEyeballsConnectionImplTest, IsHalfCloseEnabled) {
  EXPECT_CALL(*created_connections_[0], isHalfCloseEnabled()).WillOnce(Return(false));
  EXPECT_FALSE(impl_->isHalfCloseEnabled());

  EXPECT_CALL(*created_connections_[0], isHalfCloseEnabled()).WillOnce(Return(true));
  EXPECT_TRUE(impl_->isHalfCloseEnabled());

  connectFirstAttempt();
}

TEST_F(HappyEyeballsConnectionImplTest, IsHalfCloseEnabledAfterConnect) {
  connectFirstAttempt();

  EXPECT_CALL(*created_connections_[0], isHalfCloseEnabled()).WillOnce(Return(true));
  EXPECT_TRUE(impl_->isHalfCloseEnabled());
}

TEST_F(HappyEyeballsConnectionImplTest, ReadDisable) {
  // The disables will be captured by the impl and not passed to the connection until it completes.
  impl_->readDisable(true);

  startConnect();

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  timeOutAndStartNextAttempt();

  // The disables will be captured by the impl and not passed to the connection until it completes.
  impl_->readDisable(true);
  impl_->readDisable(true);
  // Read disable count should now be 2.
  impl_->readDisable(false);

  // readDisable() should be applied to the now final connection.
  EXPECT_CALL(*created_connections_[1], readDisable(true)).Times(2);
  connectSecondAttempt();

  // Verify that addBytesSentCallback() calls are delegated to the remaining connection.
  EXPECT_CALL(*created_connections_[1], readDisable(false));
  impl_->readDisable(false);
}

TEST_F(HappyEyeballsConnectionImplTest, ReadEnabled) {
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

TEST_F(HappyEyeballsConnectionImplTest, ReadEnabledAfterConnect) {
  connectFirstAttempt();

  EXPECT_CALL(*created_connections_[0], readEnabled()).WillOnce(Return(true));
  EXPECT_TRUE(impl_->readEnabled());
}

TEST_F(HappyEyeballsConnectionImplTest, StartSecureTransport) {
  EXPECT_CALL(*created_connections_[0], startSecureTransport()).WillOnce(Return(true));
  EXPECT_TRUE(impl_->startSecureTransport());

  startConnect();

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  // startSecureTransport() should be applied to the newly created connection.
  EXPECT_CALL(*next_connections_.back(), startSecureTransport()).WillOnce(Return(true));
  timeOutAndStartNextAttempt();

  EXPECT_CALL(*created_connections_[0], startSecureTransport()).WillOnce(Return(false));
  EXPECT_CALL(*created_connections_[1], startSecureTransport()).WillOnce(Return(true));
  EXPECT_FALSE(impl_->startSecureTransport());

  connectSecondAttempt();

  // Verify that startSecureTransport calls are delegated to the remaining connection.
  EXPECT_CALL(*created_connections_[1], startSecureTransport()).WillOnce(Return(false));
  EXPECT_FALSE(impl_->startSecureTransport());
}

TEST_F(HappyEyeballsConnectionImplTest, StartSecureTransportAfterConnect) {
  connectFirstAttempt();

  EXPECT_CALL(*created_connections_[0], startSecureTransport());
  impl_->startSecureTransport();
}

// Tests for HappyEyeballsConnectionImpl methods which simply delegate to the first connection.

TEST_F(HappyEyeballsConnectionImplTest, Dispatcher) {
  connectFirstAttempt();

  EXPECT_CALL(*created_connections_[0], dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
  EXPECT_EQ(&dispatcher_, &(impl_->dispatcher()));
}

TEST_F(HappyEyeballsConnectionImplTest, BufferLimit) {
  connectFirstAttempt();

  EXPECT_CALL(*created_connections_[0], bufferLimit()).WillOnce(Return(42));
  EXPECT_EQ(42, impl_->bufferLimit());
}

TEST_F(HappyEyeballsConnectionImplTest, NextProtocol) {
  connectFirstAttempt();

  EXPECT_CALL(*created_connections_[0], nextProtocol()).WillOnce(Return("h3"));
  EXPECT_EQ("h3", impl_->nextProtocol());
}

TEST_F(HappyEyeballsConnectionImplTest, AddressProvider) {
  connectFirstAttempt();

  const ConnectionInfoSetterImpl provider(std::make_shared<Address::Ipv4Instance>(80),
                                          std::make_shared<Address::Ipv4Instance>(80));
  EXPECT_CALL(*created_connections_[0], connectionInfoProvider()).WillOnce(ReturnRef(provider));
  impl_->connectionInfoProvider();
}

TEST_F(HappyEyeballsConnectionImplTest, AddressProviderSharedPtr) {
  connectFirstAttempt();

  ConnectionInfoProviderSharedPtr provider = std::make_shared<ConnectionInfoSetterImpl>(
      std::make_shared<Address::Ipv4Instance>("127.0.0.2"),
      std::make_shared<Address::Ipv4Instance>("127.0.0.1"));
  EXPECT_CALL(*created_connections_[0], connectionInfoProviderSharedPtr())
      .WillOnce(Return(provider));
  EXPECT_EQ(provider, impl_->connectionInfoProviderSharedPtr());
}

TEST_F(HappyEyeballsConnectionImplTest, UnixSocketPeerCredentials) {
  connectFirstAttempt();

  EXPECT_CALL(*created_connections_[0], unixSocketPeerCredentials())
      .WillOnce(Return(absl::optional<Connection::UnixDomainSocketPeerCredentials>()));
  EXPECT_FALSE(impl_->unixSocketPeerCredentials().has_value());
}

TEST_F(HappyEyeballsConnectionImplTest, Ssl) {
  connectFirstAttempt();

  Ssl::ConnectionInfoConstSharedPtr ssl = nullptr;
  EXPECT_CALL(*created_connections_[0], ssl()).WillOnce(Return(ssl));
  EXPECT_EQ(ssl, impl_->ssl());
}

TEST_F(HappyEyeballsConnectionImplTest, SocketOptions) {
  connectFirstAttempt();

  ConnectionSocket::OptionsSharedPtr options = nullptr;
  EXPECT_CALL(*created_connections_[0], socketOptions()).WillOnce(ReturnRef(options));
  EXPECT_EQ(options, impl_->socketOptions());
}

TEST_F(HappyEyeballsConnectionImplTest, RequestedServerName) {
  connectFirstAttempt();

  EXPECT_CALL(*created_connections_[0], requestedServerName()).WillOnce(Return("name"));
  EXPECT_EQ("name", impl_->requestedServerName());
}

TEST_F(HappyEyeballsConnectionImplTest, StreamInfo) {
  connectFirstAttempt();

  StreamInfo::MockStreamInfo info;
  EXPECT_CALL(*created_connections_[0], streamInfo()).WillOnce(ReturnRef(info));
  EXPECT_EQ(&info, &impl_->streamInfo());
}

TEST_F(HappyEyeballsConnectionImplTest, ConstStreamInfo) {
  connectFirstAttempt();

  StreamInfo::MockStreamInfo info;
  EXPECT_CALL(*created_connections_[0], streamInfo()).WillOnce(ReturnRef(info));
  const HappyEyeballsConnectionImpl* impl = impl_.get();
  EXPECT_EQ(&info, &impl->streamInfo());
}

TEST_F(HappyEyeballsConnectionImplTest, TransportFailureReason) {
  connectFirstAttempt();

  EXPECT_CALL(*created_connections_[0], transportFailureReason()).WillOnce(Return("reason"));
  EXPECT_EQ("reason", impl_->transportFailureReason());
}

TEST_F(HappyEyeballsConnectionImplTest, LastRoundTripTime) {
  connectFirstAttempt();

  absl::optional<std::chrono::milliseconds> rtt = std::chrono::milliseconds(5);
  EXPECT_CALL(*created_connections_[0], lastRoundTripTime()).WillOnce(Return(rtt));
  EXPECT_EQ(rtt, impl_->lastRoundTripTime());
}

TEST_F(HappyEyeballsConnectionImplTest, SortAddresses) {
  auto ip_v4_1 = std::make_shared<Address::Ipv4Instance>("127.0.0.1");
  auto ip_v4_2 = std::make_shared<Address::Ipv4Instance>("127.0.0.2");
  auto ip_v4_3 = std::make_shared<Address::Ipv4Instance>("127.0.0.3");
  auto ip_v4_4 = std::make_shared<Address::Ipv4Instance>("127.0.0.4");

  auto ip_v6_1 = std::make_shared<Address::Ipv6Instance>("ff02::1", 0);
  auto ip_v6_2 = std::make_shared<Address::Ipv6Instance>("ff02::2", 0);
  auto ip_v6_3 = std::make_shared<Address::Ipv6Instance>("ff02::3", 0);
  auto ip_v6_4 = std::make_shared<Address::Ipv6Instance>("ff02::4", 0);

  // All v4 address so unchanged.
  std::vector<Address::InstanceConstSharedPtr> v4_list = {ip_v4_1, ip_v4_2, ip_v4_3, ip_v4_4};
  EXPECT_EQ(v4_list, HappyEyeballsConnectionImpl::sortAddresses(v4_list));

  // All v6 address so unchanged.
  std::vector<Address::InstanceConstSharedPtr> v6_list = {ip_v6_1, ip_v6_2, ip_v6_3, ip_v6_4};
  EXPECT_EQ(v6_list, HappyEyeballsConnectionImpl::sortAddresses(v6_list));

  std::vector<Address::InstanceConstSharedPtr> v6_then_v4 = {ip_v6_1, ip_v6_2, ip_v4_1, ip_v4_2};
  std::vector<Address::InstanceConstSharedPtr> interleaved = {ip_v6_1, ip_v4_1, ip_v6_2, ip_v4_2};
  EXPECT_EQ(interleaved, HappyEyeballsConnectionImpl::sortAddresses(v6_then_v4));

  std::vector<Address::InstanceConstSharedPtr> v6_then_single_v4 = {ip_v6_1, ip_v6_2, ip_v6_3,
                                                                    ip_v4_1};
  std::vector<Address::InstanceConstSharedPtr> interleaved2 = {ip_v6_1, ip_v4_1, ip_v6_2, ip_v6_3};
  EXPECT_EQ(interleaved2, HappyEyeballsConnectionImpl::sortAddresses(v6_then_single_v4));

  std::vector<Address::InstanceConstSharedPtr> mixed = {ip_v6_1, ip_v6_2, ip_v6_3, ip_v4_1,
                                                        ip_v4_2, ip_v4_3, ip_v4_4, ip_v6_4};
  std::vector<Address::InstanceConstSharedPtr> interleaved3 = {ip_v6_1, ip_v4_1, ip_v6_2, ip_v4_2,
                                                               ip_v6_3, ip_v4_3, ip_v6_4, ip_v4_4};
  EXPECT_EQ(interleaved3, HappyEyeballsConnectionImpl::sortAddresses(mixed));
}

} // namespace Network
} // namespace Envoy
