#include "source/common/network/happy_eyeballs_connection_impl.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/transport_socket_options_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/transport_socket.h"
#include "test/mocks/network/connection.h"

using testing::Return;
using testing::StrictMock;

namespace Envoy {
namespace Network {

class HappyEyeballsConnectionImplTest : public testing::Test {
 public:
  HappyEyeballsConnectionImplTest()
      : failover_timer_(new testing::StrictMock<Event::MockTimer>(&dispatcher_)),
        transport_socket_options_(std::make_shared<TransportSocketOptionsImpl>()),
        options_(std::make_shared<ConnectionSocket::Options>()),
        address_list_({
          std::make_shared<Address::Ipv4Instance>("127.0.0.1"),
          std::make_shared<Address::Ipv4Instance>("127.0.0.2"),
          std::make_shared<Address::Ipv4Instance>("127.0.0.3") }) {
    EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
    EXPECT_CALL(dispatcher_, createClientConnection_(address_list_[0], _, _, _)).WillOnce(
        testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));

    // This timer will be returned and armed as the happy eyeballs connection creates the next connection timer.
    EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(1);
    EXPECT_CALL(*failover_timer_, enabled()).WillRepeatedly(Return(true));
    next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
    std::cerr << __LINE__ << std::endl;
    impl_ = std::make_unique<HappyEyeballsConnectionImpl>(dispatcher_,
                                                          address_list_,
                                                          Address::InstanceConstSharedPtr(),
                                                          transport_socket_factory_,
                                                          transport_socket_options_,
                                                          options_);
    std::cerr << __LINE__ << std::endl;

  }

  MockClientConnection* createNextConnection() {
    std::cerr << __LINE__ << " size: " << next_connections_.size() <<  std::endl;
    created_connections_.push_back(next_connections_.front().release());
    next_connections_.pop_front();
    EXPECT_CALL(*created_connections_.back(), addConnectionCallbacks(_)).WillOnce(Invoke([&](ConnectionCallbacks& cb) -> void { connection_callbacks_.push_back(&cb);}));
    return created_connections_.back();
  }

 protected:
  Event::MockDispatcher dispatcher_;
  testing::StrictMock<Event::MockTimer>* failover_timer_;
  MockTransportSocketFactory transport_socket_factory_;
  TransportSocketOptionsSharedPtr transport_socket_options_;
  const ConnectionSocket::OptionsSharedPtr options_;
  const std::vector<Address::InstanceConstSharedPtr> address_list_;
  std::vector<StrictMock<MockClientConnection>*> created_connections_;
  std::vector<ConnectionCallbacks*> connection_callbacks_;
  std::deque<std::unique_ptr<StrictMock<MockClientConnection>>> next_connections_;
  std::unique_ptr<HappyEyeballsConnectionImpl> impl_;
};

TEST_F(HappyEyeballsConnectionImplTest, Connect) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();
}

TEST_F(HappyEyeballsConnectionImplTest, ConnectTimeout) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  // Let the first attempt timeout to start the second attempt.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(address_list_[1], _, _, _)).WillOnce(
      testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(1);
  failover_timer_->invokeCallback();

  // Let the second attempt timeout to start the third and final attempt.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(address_list_[2], _, _, _)).WillOnce(
      testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  // Since there are no more address to connect to, the fallback timer will not
  // be rescheduled.
  failover_timer_->invokeCallback();
}

TEST_F(HappyEyeballsConnectionImplTest, ConnectFailed) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(
      testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[0], close(ConnectionCloseType::NoFlush));
  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(1);
  connection_callbacks_[0]->onEvent(ConnectionEvent::RemoteClose);
}


TEST_F(HappyEyeballsConnectionImplTest, ConnectFirstSuccess) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);
}

TEST_F(HappyEyeballsConnectionImplTest, ConnectTimeoutThenFirstSuccess) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  // Let the first attempt timeout to start the second attempt.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(
      testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(1);
  failover_timer_->invokeCallback();

  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[1], close(ConnectionCloseType::NoFlush));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);

  // Verify that calls are delegated to the right connection.
  EXPECT_CALL(*created_connections_[0], connecting()).WillOnce(Return(false));
  EXPECT_FALSE(impl_->connecting());
}

TEST_F(HappyEyeballsConnectionImplTest, ConnectTimeoutThenSecondSuccess) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  // Let the first attempt timeout to start the second attempt.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(
      testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(1);
  failover_timer_->invokeCallback();

  EXPECT_CALL(*created_connections_[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[0], close(ConnectionCloseType::NoFlush));
  connection_callbacks_[1]->onEvent(ConnectionEvent::Connected);

  // Verify that calls are delegated to the right connection.
  EXPECT_CALL(*created_connections_[1], connecting()).WillOnce(Return(false));
  EXPECT_FALSE(impl_->connecting());
}

TEST_F(HappyEyeballsConnectionImplTest, ConnectTimeoutThenSecondFailsAndFirstSucceeds) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  // Let the first attempt timeout to start the second attempt.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(address_list_[1], _, _, _)).WillOnce(
      testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(1);
  failover_timer_->invokeCallback();

  // When the second attempt fails, the third and final attempt will be started.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(address_list_[2], _, _, _)).WillOnce(
      testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  EXPECT_CALL(*failover_timer_, disableTimer());
  // Since there are no more address to connect to, the fallback timer will not
  // be rescheduled.
  ASSERT_EQ(2, created_connections_.size());
  EXPECT_CALL(*created_connections_[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[1], close(ConnectionCloseType::NoFlush));
  std::cerr << __FUNCTION__ << ":" << __LINE__ << " \n";
  connection_callbacks_[1]->onEvent(ConnectionEvent::RemoteClose);
  std::cerr << __FUNCTION__ << ":" << __LINE__ << " \n";

  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[0], close(ConnectionCloseType::NoFlush));
  std::cerr << __FUNCTION__ << ":" << __LINE__ << " \n";
  connection_callbacks_[0]->onEvent(ConnectionEvent::RemoteClose);
  std::cerr << __FUNCTION__ << ":" << __LINE__ << " \n";
}

TEST_F(HappyEyeballsConnectionImplTest, NoDelay) {
  EXPECT_CALL(*created_connections_[0], noDelay(true));
  impl_->noDelay(true);

  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  // Let the first attempt timeout to start the second attempt.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(
      testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  // noDelay() should be applied to the newly created connection.
  EXPECT_CALL(*next_connections_.back(), noDelay(true));
  EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(1);
  failover_timer_->invokeCallback();

  EXPECT_CALL(*created_connections_[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[0], close(ConnectionCloseType::NoFlush));
  connection_callbacks_[1]->onEvent(ConnectionEvent::Connected);

  // Verify that noDelay calls are delegated to the remaingin connection.
  EXPECT_CALL(*created_connections_[1], noDelay(false));
  impl_->noDelay(false);
}

TEST_F(HappyEyeballsConnectionImplTest, DetectEarlyCloseWhenReadDisabled){
  EXPECT_CALL(*created_connections_[0], detectEarlyCloseWhenReadDisabled(true));
  impl_->detectEarlyCloseWhenReadDisabled(true);

  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  // Let the first attempt timeout to start the second attempt.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(
      testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  // detectEarlyCloseWhenReadDisabled() should be applied to the newly created connection.
  EXPECT_CALL(*next_connections_.back(), detectEarlyCloseWhenReadDisabled(true));
  EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(1);
  failover_timer_->invokeCallback();

  EXPECT_CALL(*created_connections_[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[0], close(ConnectionCloseType::NoFlush));
  connection_callbacks_[1]->onEvent(ConnectionEvent::Connected);

  // Verify that detectEarlyCloseWhenReadDisabled calls are delegated to the remaingin connection.
  EXPECT_CALL(*created_connections_[1], detectEarlyCloseWhenReadDisabled(false));
  impl_->detectEarlyCloseWhenReadDisabled(false);
}

TEST_F(HappyEyeballsConnectionImplTest, WriteBeforeConnect) {
  Buffer::OwnedImpl data("hello world");
  bool end_stream = false;

  impl_->write(data, end_stream);

  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  // The call to write() will be replayed on the underlying connection.
  EXPECT_CALL(*created_connections_[0], write(_, _)).WillOnce(
      Invoke([](Buffer::Instance& data, bool end_stream) -> void {
        EXPECT_EQ("hello world", data.toString());
        EXPECT_FALSE(end_stream);
        ;})
                                                              );
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);
}

} // namespace Network
} // namespace Envoy
