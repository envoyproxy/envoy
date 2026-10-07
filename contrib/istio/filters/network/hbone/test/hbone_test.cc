#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/connection_impl.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/io_socket/user_space/io_handle_impl.h"

#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "contrib/istio/filters/network/hbone/source/hbone.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Istio {
namespace Hbone {
namespace {

using testing::_;
using testing::StrictMock;

class HboneSocketTest : public testing::Test {
protected:
  HboneSocketTest()
      : api_(Api::createApiForTest(time_system_)), dispatcher_(api_->allocateDispatcher("test")) {
    auto pair = IoSocket::UserSpace::IoHandleFactory::createBufferLimitedIoHandlePair(1024);
    peer_ = std::move(pair.second);
    auto address = std::make_shared<Network::Address::EnvoyInternalInstance>("hbone");
    auto socket =
        std::make_unique<UpstreamSocket>(state_, nullptr, StreamInfo::FilterState::Objects{});
    transport_ = socket.get();
    connection_ = std::make_unique<Network::ClientConnectionImpl>(
        *dispatcher_,
        std::make_unique<Network::ConnectionSocketImpl>(std::move(pair.first), address, address),
        address, std::move(socket), nullptr, nullptr);
    connection_->addConnectionCallbacks(callbacks_);
  }

  ~HboneSocketTest() override {
    connection_->removeConnectionCallbacks(callbacks_);
    connection_->close(Network::ConnectionCloseType::NoFlush);
    if (peer_->isOpen()) {
      peer_->close();
    }
    dispatcher_->clearDeferredDeleteList();
  }

  void connect() {
    connection_->connect();
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  void accept() {
    EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::Connected));
    state_->accept();
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    EXPECT_EQ(ConnectState::Phase::Connected, state_->phase());
  }

  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  ConnectStateSharedPtr state_{std::make_shared<ConnectState>()};
  IoSocket::UserSpace::IoHandleImplPtr peer_;
  UpstreamSocket* transport_{};
  Network::ClientConnectionPtr connection_;
  StrictMock<Network::MockConnectionCallbacks> callbacks_;
};

TEST_F(HboneSocketTest, HoldsReadinessAndWritesUntilConnectAccepted) {
  connect();
  Buffer::OwnedImpl request("POST / HTTP/1.1\r\nContent-Length: 7\r\n\r\npayload");
  connection_->write(request, false);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(0, peer_->getReceiveBuffer()->length());
  EXPECT_FALSE(transport_->canFlushClose());

  accept();
  EXPECT_EQ("POST / HTTP/1.1\r\nContent-Length: 7\r\n\r\npayload",
            peer_->getReceiveBuffer()->toString());
  EXPECT_TRUE(transport_->canFlushClose());
}

TEST_F(HboneSocketTest, AcceptBeforeInternalConnect) {
  state_->accept();
  EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::Connected));
  connect();
  EXPECT_EQ(ConnectState::Phase::Connected, state_->phase());
}

TEST_F(HboneSocketTest, FailureDoesNotReleasePayload) {
  connect();
  Buffer::OwnedImpl request("payload");
  connection_->write(request, false);
  EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::RemoteClose));
  state_->fail("tunnel_response:503");
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
  EXPECT_EQ("tunnel_response:503", connection_->transportFailureReason());
  EXPECT_EQ(0, peer_->getReceiveBuffer()->length());
}

TEST_F(HboneSocketTest, CloseAfterAcceptanceBeforeReadinessWins) {
  connect();
  Buffer::OwnedImpl request("payload");
  connection_->write(request, false);
  state_->accept();
  state_->fail("stream reset after CONNECT 200");
  // The scheduled success callback must observe the intervening failure.
  EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::RemoteClose));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(0, peer_->getReceiveBuffer()->length());
  EXPECT_EQ(ConnectState::Phase::Failed, state_->phase());
}

TEST_F(HboneSocketTest, CancellationRemovesScheduledReadiness) {
  connect();
  state_->accept();
  EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  connection_->close(Network::ConnectionCloseType::NoFlush);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(ConnectState::Phase::Failed, state_->phase());
  state_->accept();
  EXPECT_EQ(ConnectState::Phase::Failed, state_->phase());
}

TEST_F(HboneSocketTest, PeerCloseWithoutForwardingFilter) {
  connect();
  EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::RemoteClose));
  peer_->close();
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

TEST_F(HboneSocketTest, StateStaysLocalToInternalPair) {
  envoy::config::core::v3::Metadata metadata;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  peer_->passthroughState()->mergeInto(metadata, filter_state);
  EXPECT_EQ(state_.get(), filter_state.getDataReadOnly<ConnectState>(ConnectState::Key));
  EXPECT_TRUE(filter_state.objectsSharedWithUpstreamConnection()->empty());
}

TEST_F(HboneSocketTest, EstablishedConnectionPreservesHalfCloseAndBufferedData) {
  connect();
  accept();
  // Once open, a filter close notification must not override normal I/O.
  state_->fail("peer closed");
  EXPECT_EQ(ConnectState::Phase::Connected, state_->phase());
  Buffer::OwnedImpl request("payload");
  connection_->enableHalfClose(true);
  connection_->write(request, true);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ("payload", peer_->getReceiveBuffer()->toString());
  EXPECT_TRUE(peer_->hasReceivedEof());

  Buffer::OwnedImpl response("reply");
  ASSERT_TRUE(peer_->write(response).ok());
  ASSERT_EQ(0, peer_->shutdown(ENVOY_SHUT_WR).return_value_);
  Buffer::OwnedImpl received;
  const auto result = transport_->doRead(received);
  EXPECT_EQ("reply", received.toString());
  EXPECT_TRUE(result.end_stream_read_);
}

TEST_F(HboneSocketTest, ServerFirstDataAndHalfCloseWaitForAcceptedReadiness) {
  connect();
  connection_->enableHalfClose(true);
  connection_->readDisable(true);
  Buffer::OwnedImpl greeting("greeting");
  ASSERT_TRUE(peer_->write(greeting).ok());
  Buffer::OwnedImpl received;
  EXPECT_EQ(0, transport_->doRead(received).bytes_processed_);
  EXPECT_EQ(0, received.length());

  state_->accept();
  ASSERT_EQ(0, peer_->shutdown(ENVOY_SHUT_WR).return_value_);
  EXPECT_EQ(0, transport_->doRead(received).bytes_processed_);
  accept();
  const auto result = transport_->doRead(received);
  EXPECT_EQ("greeting", received.toString());
  EXPECT_TRUE(result.end_stream_read_);
}

TEST_F(HboneSocketTest, DuplicateAcceptanceDoesNotRaiseConnectedAgain) {
  connect();
  accept();
  state_->accept();
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

TEST_F(HboneSocketTest, RejectsNativeSocketsBeforeConnecting) {
  StrictMock<Network::MockIoHandle> native_io;
  StrictMock<Network::MockTransportSocketCallbacks> transport_callbacks;
  EXPECT_CALL(transport_callbacks, ioHandle()).WillRepeatedly(testing::ReturnRef(native_io));
  EXPECT_CALL(transport_callbacks, connection()).WillRepeatedly(testing::ReturnRef(*connection_));
  auto state = std::make_shared<ConnectState>();
  UpstreamSocket socket(state, nullptr, {});
  socket.setTransportSocketCallbacks(transport_callbacks);
  StrictMock<Network::MockConnectionSocket> connection_socket;
  EXPECT_EQ(-1, socket.connect(connection_socket).return_value_);
  EXPECT_EQ(ConnectState::Phase::Failed, state->phase());
  EXPECT_EQ("hbone shim requires an internal listener", socket.failureReason());
}

} // namespace
} // namespace Hbone
} // namespace Istio
} // namespace Extensions
} // namespace Envoy
