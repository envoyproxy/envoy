#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/buffer/watermark_buffer.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/address_impl.h"
#include "common/network/buffer_source_socket.h"
#include "common/network/connection_impl.h"
#include "common/network/io_socket_handle_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/pipe_connection_impl.h"
#include "common/network/utility.h"
#include "common/runtime/runtime_impl.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::DoAll;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::SaveArg;
using testing::Sequence;
using testing::StrictMock;

namespace Envoy {
namespace Network {
namespace {

class PipeConnectionImplTest : public testing::TestWithParam<Address::IpVersion> {
protected:
  PipeConnectionImplTest()
      : api_(Api::createApiForTest(time_system_)), stream_info_(time_system_) {}

  void setupPipe() {
    if (dispatcher_.get() == nullptr) {
      dispatcher_ = api_->allocateDispatcher("test_thread");
    }

    auto client_socket = std::make_unique<Network::BufferSourceSocket>();
    auto server_socket = std::make_unique<Network::BufferSourceSocket>();
    auto client_socket_raw = client_socket.get();
    auto server_socket_raw = server_socket.get();
    auto client_conn = std::make_unique<Network::ClientPipeImpl>(
        *dispatcher_, server_address_, client_address_, std::move(client_socket),
        *client_socket_raw, *client_socket_raw, nullptr);
    ENVOY_LOG_MISC(debug, "lambdai: client pipe C{} owns TS{} and B{}", client_conn->id(),
                   client_socket_raw->bsid(), client_socket_raw->read_buffer_.bid());
    client_conn->addConnectionCallbacks(client_callbacks_);

    auto server_conn = std::make_unique<Network::ServerPipeImpl>(
        *dispatcher_, client_address_, server_address_, std::move(server_socket),
        *server_socket_raw, *server_socket_raw, nullptr);
    ENVOY_LOG_MISC(debug, "lambdai: server pipe C{} owns TS{} and B{}", server_conn->id(),
                   server_socket_raw->bsid(), server_socket_raw->read_buffer_.bid());

    server_conn->setPeer(client_conn.get());
    client_conn->setPeer(server_conn.get());
    // TODO(lambdai): Retrieve buffer each time when supporting close.
    // TODO(lambdai): Add to dest buffer to generic IoHandle, or TransportSocketCallback.
    client_socket_raw->setWritablePeer(server_socket_raw);
    client_socket_raw->setEventSchedulable(client_conn.get());
    server_socket_raw->setWritablePeer(client_socket_raw);
    server_socket_raw->setEventSchedulable(server_conn.get());

    server_connection_ = std::move(server_conn);
    client_connection_ = std::move(client_conn);
    server_socket_ = server_socket_raw;
    client_socket_ = client_socket_raw;
  }

  void doConnect() {
    Network::ConnectionSocketPtr socket =
        std::make_unique<Network::ConnectionSocketImpl>(nullptr,
                                                        // Local
                                                        server_address_,
                                                        // Remote
                                                        client_address_);
    // TODO(lambdai): setup filter chain based on socket
    UNREFERENCED_PARAMETER(socket);

    server_stream_info_ = std::make_unique<StreamInfo::StreamInfoImpl>(
        dispatcher_->timeSource(), StreamInfo::FilterState::LifeSpan::Connection);
    server_connection_->setStreamInfo(server_stream_info_.get());

    server_connection_->addConnectionCallbacks(server_callbacks_);
    read_filter_ = std::make_shared<NiceMock<MockReadFilter>>();
    server_connection_->addReadFilter(read_filter_);

    // TODO(lambdai): scheduleNextEvent() should automatically trigger Write at established.
    client_connection_->enableWriteRead();
    //    client_connection_->scheduleNextEvent();
    server_connection_->enableWriteRead();
    //    server_connection_->scheduleNextEvent();
  }

  void disconnect(bool wait_for_remote_close) {
    if (client_write_buffer_) {
      EXPECT_CALL(*client_write_buffer_, drain(_))
          .Times(AnyNumber())
          .WillOnce(Invoke([&](uint64_t size) -> void { client_write_buffer_->baseDrain(size); }));
    }
    EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));
    client_connection_->close(ConnectionCloseType::NoFlush);
    if (wait_for_remote_close) {
      EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose))
          .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));

      dispatcher_->run(Event::Dispatcher::RunType::Block);
    } else {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  void useMockBuffer() {
    // This needs to be called before the dispatcher is created.
    ASSERT(dispatcher_.get() == nullptr);

    MockBufferFactory* factory = new StrictMock<MockBufferFactory>;
    dispatcher_ = api_->allocateDispatcher("test_thread", Buffer::WatermarkFactoryPtr{factory});
    // The first call to create a client session will get a MockBuffer.
    // Other calls for server sessions will by default get a normal OwnedImpl.
    EXPECT_CALL(*factory, create_(_, _, _))
        .Times(AnyNumber())
        .WillOnce(Invoke([&](std::function<void()> below_low, std::function<void()> above_high,
                             std::function<void()> above_overflow) -> Buffer::Instance* {
          client_write_buffer_ = new MockWatermarkBuffer(below_low, above_high, above_overflow);
          return client_write_buffer_;
        }))
        .WillRepeatedly(Invoke([](std::function<void()> below_low, std::function<void()> above_high,
                                  std::function<void()> above_overflow) -> Buffer::Instance* {
          return new Buffer::WatermarkBuffer(below_low, above_high, above_overflow);
        }));
  }

protected:
  struct ConnectionMocks {
    std::unique_ptr<NiceMock<Event::MockDispatcher>> dispatcher_;
    Event::MockTimer* timer_;
    std::unique_ptr<NiceMock<MockTransportSocket>> transport_socket_;
    NiceMock<Event::MockFileEvent>* file_event_;
    Event::FileReadyCb* file_ready_cb_;
  };

  ConnectionMocks createConnectionMocks(bool create_timer = true) {
    auto dispatcher = std::make_unique<NiceMock<Event::MockDispatcher>>();
    EXPECT_CALL(dispatcher->buffer_factory_, create_(_, _, _))
        .WillRepeatedly(Invoke([](std::function<void()> below_low, std::function<void()> above_high,
                                  std::function<void()> above_overflow) -> Buffer::Instance* {
          // ConnectionImpl calls Envoy::MockBufferFactory::create(), which calls create_() and
          // wraps the returned raw pointer below with a unique_ptr.
          return new Buffer::WatermarkBuffer(below_low, above_high, above_overflow);
        }));

    Event::MockTimer* timer = nullptr;
    if (create_timer) {
      // This timer will be returned (transferring ownership) to the ConnectionImpl when
      // createTimer() is called to allocate the delayed close timer.
      timer = new Event::MockTimer(dispatcher.get());
    }

    NiceMock<Event::MockFileEvent>* file_event = new NiceMock<Event::MockFileEvent>;
    EXPECT_CALL(*dispatcher, createFileEvent_(0, _, _, _))
        .WillOnce(DoAll(SaveArg<1>(&file_ready_cb_), Return(file_event)));

    auto transport_socket = std::make_unique<NiceMock<MockTransportSocket>>();
    EXPECT_CALL(*transport_socket, canFlushClose()).WillRepeatedly(Return(true));

    return ConnectionMocks{std::move(dispatcher), timer, std::move(transport_socket), file_event,
                           &file_ready_cb_};
  }

  Event::FileReadyCb file_ready_cb_;
  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::shared_ptr<Network::TcpListenSocket> socket_{nullptr};
  std::unique_ptr<StreamInfo::StreamInfoImpl> server_stream_info_;
  Network::MockListenerCallbacks listener_callbacks_;
  Network::MockConnectionHandler connection_handler_;
  Network::ListenerPtr listener_;
  std::unique_ptr<Network::ClientPipeImpl> client_connection_;
  StrictMock<MockConnectionCallbacks> client_callbacks_;
  std::unique_ptr<Network::ServerPipeImpl> server_connection_;
  StrictMock<Network::MockConnectionCallbacks> server_callbacks_;
  std::shared_ptr<MockReadFilter> read_filter_;
  MockWatermarkBuffer* client_write_buffer_ = nullptr;
  Address::InstanceConstSharedPtr source_address_;
  Socket::OptionsSharedPtr socket_options_;
  StreamInfo::StreamInfoImpl stream_info_;
  Address::InstanceConstSharedPtr client_address_{
      std::make_shared<Network::Address::EnvoyInternalInstance>("client_address")};
  Address::InstanceConstSharedPtr server_address_{
      std::make_shared<Network::Address::EnvoyInternalInstance>("server_address")};
  // The below sockets are owned by connection. Don't manually destroy.
  Network::BufferSourceSocket* client_socket_;
  Network::BufferSourceSocket* server_socket_;
};

TEST_P(PipeConnectionImplTest, UniqueId) {
  setupPipe();
  uint64_t client_id = client_connection_->id();
  uint64_t server_id = server_connection_->id();
  EXPECT_NE(server_id, client_id);

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));
  client_connection_->close(ConnectionCloseType::NoFlush);
}

TEST_P(PipeConnectionImplTest, ClientClose) {
  setupPipe();
  doConnect();
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::Connected));
  Buffer::OwnedImpl buffer("hello world");
  client_connection_->write(buffer, false);
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  client_connection_->close(ConnectionCloseType::NoFlush);
}

// Ensure the new counter logic in ReadDisable avoids tripping asserts in ReadDisable guarding
// against actual enabling twice in a row.
// TODO(lambdai): Reenable when there is clean side affect of readDisable(_).
TEST_P(PipeConnectionImplTest, DISABLED_ReadDisable) {
  auto dispatcher = std::make_unique<NiceMock<Event::MockDispatcher>>();
  EXPECT_CALL(dispatcher->buffer_factory_, create_(_, _, _))
      .WillRepeatedly(Invoke([](std::function<void()> below_low, std::function<void()> above_high,
                                std::function<void()> above_overflow) -> Buffer::Instance* {
        // ConnectionImpl calls Envoy::MockBufferFactory::create(), which calls create_() and
        // wraps the returned raw pointer below with a unique_ptr.
        return new Buffer::WatermarkBuffer(below_low, above_high, above_overflow);
      }));

  auto mock_writable_peer = std::make_unique<MockWritablePeer>();
  auto mock_readable_source = std::make_unique<MockReadableSource>();

  auto transport_socket = std::make_unique<NiceMock<MockTransportSocket>>();
  EXPECT_CALL(*transport_socket, canFlushClose()).WillRepeatedly(Return(true));
  auto& transport_socket_ref = *transport_socket;
  auto connection = std::make_unique<Network::ServerPipeImpl>(
      *dispatcher, server_address_, client_address_, std::move(transport_socket),
      *mock_writable_peer, *mock_readable_source, nullptr);
  connection->setStreamInfo(&stream_info_);
  ON_CALL(transport_socket_ref, doRead(_))
      .WillByDefault(Return(Network::IoResult{Network::PostIoAction::KeepOpen, 0, false}));

  EXPECT_CALL(transport_socket_ref, doRead(_)).Times(0);
  connection->readDisable(true);
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_CALL(transport_socket_ref, doRead(_)).Times(1);
  connection->readDisable(false);
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(transport_socket_ref, doRead(_)).Times(0);
  connection->readDisable(true);
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(transport_socket_ref, doRead(_)).Times(0);
  connection->readDisable(true);
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_CALL(transport_socket_ref, doRead(_)).Times(0);
  connection->readDisable(false);
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_CALL(transport_socket_ref, doRead(_)).Times(1);
  connection->readDisable(false);
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(transport_socket_ref, doRead(_)).Times(0);
  connection->readDisable(true);
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_CALL(transport_socket_ref, doRead(_)).Times(0);
  connection->readDisable(true);
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_CALL(transport_socket_ref, doRead(_)).Times(0);
  connection->readDisable(false);
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_CALL(transport_socket_ref, doRead(_)).Times(0);
  connection->readDisable(true);
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_CALL(transport_socket_ref, doRead(_)).Times(0);
  connection->readDisable(false);
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_CALL(transport_socket_ref, doRead(_)).Times(1);
  connection->readDisable(false);
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);

  connection->close(ConnectionCloseType::NoFlush);
}

// The HTTP/1 codec handles pipelined connections by relying on readDisable(false) resulting in the
// subsequent request being dispatched. Regression test this behavior.
TEST_P(PipeConnectionImplTest, ReadEnableDispatches) {
  setupPipe();
  doConnect();
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::Connected));

  auto client_read_filter = std::make_shared<NiceMock<MockReadFilter>>();
  client_connection_->addReadFilter(client_read_filter);

  {
    Buffer::OwnedImpl buffer("data");
    EXPECT_CALL(*client_read_filter, onData(BufferStringEqual("data"), false))
        .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
          dispatcher_->exit();
          return FilterStatus::StopIteration;
        }));
    server_connection_->write(buffer, false);
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  {
    client_connection_->readDisable(true);
    EXPECT_CALL(*client_read_filter, onData(BufferStringEqual("data"), false))
        .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> FilterStatus {
          buffer.drain(buffer.length());
          dispatcher_->exit();
          return FilterStatus::StopIteration;
        }));
    client_connection_->readDisable(false);
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  client_connection_->close(ConnectionCloseType::NoFlush);
}

// Make sure if we readDisable(true) and schedule a 'kick' and then
// readDisable(false) the kick doesn't happen.
TEST_P(PipeConnectionImplTest, KickUndone) {
  setupPipe();
  client_connection_->setBufferLimits(100);
  doConnect();
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::Connected));
  client_connection_->setConnected();
  auto client_read_filter = std::make_shared<NiceMock<MockReadFilter>>();
  client_connection_->addReadFilter(client_read_filter);
  Buffer::Instance& connection_buffer = client_connection_->getReadBuffer().buffer;

  {
    Buffer::OwnedImpl buffer("data");
    EXPECT_CALL(*client_read_filter, onData(BufferStringEqual("data"), false))
        .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> FilterStatus {
          dispatcher_->exit();
          ENVOY_LOG_MISC(debug, "lambdai: client_read_filter ondata");
          ASSERT(&connection_buffer == &buffer);
          return FilterStatus::StopIteration;
        }));
    server_connection_->write(buffer, false);
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  {
    // Like ReadEnableDispatches above, read disable and read enable to kick off
    // an extra read. But then readDisable again and make sure the kick doesn't
    // happen.
    ENVOY_LOG_MISC(debug, "lambdai: testing schedule latched read buffer but disabled read after");
    client_connection_->readDisable(true);
    client_connection_->readDisable(false); // Sets dispatch_buffered_data_
    client_connection_->readDisable(true);
    EXPECT_CALL(*client_read_filter, onData(_, _)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Now drain the connection's buffer and try to do a read which should _not_
  // pass up the stack (no data is read)
  {
    ENVOY_LOG_MISC(debug, "lambdai: testing enable to read but no data to read");
    connection_buffer.drain(connection_buffer.length());
    client_connection_->readDisable(false);
    EXPECT_CALL(*client_read_filter, onData(_, _)).Times(0);
    // Data no longer buffered - even if dispatch_buffered_data_ lingered it should have no effect.
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  ENVOY_LOG_MISC(debug, "lambdai: closing...");
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  client_connection_->close(ConnectionCloseType::NoFlush);
}

// Ensure that calls to readDisable on a closed connection are handled gracefully. Known past issues
// include a crash on https://github.com/envoyproxy/envoy/issues/3639, and ASSERT failure followed
// by infinite loop in https://github.com/envoyproxy/envoy/issues/9508
TEST_P(PipeConnectionImplTest, ReadDisableAfterCloseHandledGracefully) {
  setupPipe();
  doConnect();

  //   auto client_read_filter = std::make_shared<NiceMock<MockReadFilter>>();
  //   client_connection_->addReadFilter(client_read_filter);
  client_connection_->readDisable(true);
  client_connection_->readDisable(false);

  client_connection_->readDisable(true);
  client_connection_->readDisable(true);
  client_connection_->readDisable(false);
  client_connection_->readDisable(false);

  client_connection_->readDisable(true);
  client_connection_->readDisable(true);

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  client_connection_->close(ConnectionCloseType::NoFlush);

#ifndef NDEBUG
  // When running in debug mode, verify that calls to readDisable and readEnabled on a closed socket
  // trigger ASSERT failures.
  EXPECT_DEBUG_DEATH(client_connection_->readEnabled(), "");
  EXPECT_DEBUG_DEATH(client_connection_->readDisable(true), "");
  EXPECT_DEBUG_DEATH(client_connection_->readDisable(false), "");
#else
  // When running in release mode, verify that calls to readDisable change the readEnabled state.
  client_connection_->readDisable(false);
  client_connection_->readDisable(true);
  client_connection_->readDisable(false);
  EXPECT_FALSE(client_connection_->readEnabled());
  client_connection_->readDisable(false);
  EXPECT_TRUE(client_connection_->readEnabled());
#endif
}

// // On our current macOS build, the client connection does not get the early
// // close notification and instead gets the close after reading the FIN.
// // The Windows backend in libevent does not support the EV_CLOSED flag
// // so it won't detect the early close
// #if !defined(__APPLE__) && !defined(WIN32)
// TEST_P(PipeConnectionImplTest, EarlyCloseOnReadDisabledConnection) {
//   setUpBasicConnection();
//   connect();

//   client_connection_->readDisable(true);

//   EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose))
//       .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));
//   EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose));
//   server_connection_->close(ConnectionCloseType::FlushWrite);
//   dispatcher_->run(Event::Dispatcher::RunType::Block);
// }
// #endif

// TEST_P(PipeConnectionImplTest, CloseOnReadDisableWithoutCloseDetection) {
//   setUpBasicConnection();
//   connect();

//   client_connection_->detectEarlyCloseWhenReadDisabled(false);
//   client_connection_->readDisable(true);

//   EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose)).Times(0);
//   EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose))
//       .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));
//   server_connection_->close(ConnectionCloseType::FlushWrite);
//   dispatcher_->run(Event::Dispatcher::RunType::Block);

//   client_connection_->readDisable(false);
//   EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose))
//       .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));
//   dispatcher_->run(Event::Dispatcher::RunType::Block);
// }

// // Test that connection half-close is sent and received properly.
// TEST_P(PipeConnectionImplTest, HalfClose) {
//   setUpBasicConnection();
//   connect();

//   std::shared_ptr<MockReadFilter> client_read_filter(new NiceMock<MockReadFilter>());
//   server_connection_->enableHalfClose(true);
//   client_connection_->enableHalfClose(true);
//   client_connection_->addReadFilter(client_read_filter);

//   EXPECT_CALL(*read_filter_, onData(_, true)).WillOnce(InvokeWithoutArgs([&]() -> FilterStatus
//   {
//     dispatcher_->exit();
//     return FilterStatus::StopIteration;
//   }));

//   Buffer::OwnedImpl empty_buffer;
//   client_connection_->write(empty_buffer, true);
//   dispatcher_->run(Event::Dispatcher::RunType::Block);

//   Buffer::OwnedImpl buffer("data");
//   server_connection_->write(buffer, false);
//   EXPECT_CALL(*client_read_filter, onData(BufferStringEqual("data"), false))
//       .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> FilterStatus {
//         buffer.drain(buffer.length());
//         dispatcher_->exit();
//         return FilterStatus::StopIteration;
//       }));
//   dispatcher_->run(Event::Dispatcher::RunType::Block);

//   EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose));
//   EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose));
//   server_connection_->write(empty_buffer, true);
//   EXPECT_CALL(*client_read_filter, onData(BufferStringEqual(""), true))
//       .WillOnce(InvokeWithoutArgs([&]() -> FilterStatus {
//         dispatcher_->exit();
//         return FilterStatus::StopIteration;
//       }));
//   dispatcher_->run(Event::Dispatcher::RunType::Block);
// }

// Test that as watermark levels are changed, the appropriate callbacks are triggered.
TEST_P(PipeConnectionImplTest, WriteWatermarks) {
  useMockBuffer();

  setupPipe();
  doConnect();

  //   auto client_read_filter = std::make_shared<NiceMock<MockReadFilter>>();
  //   client_connection_->addReadFilter(client_read_filter);
  EXPECT_FALSE(client_connection_->aboveHighWatermark());

  // Stick 5 bytes in the connection buffer.
  std::unique_ptr<Buffer::OwnedImpl> buffer(new Buffer::OwnedImpl("hello"));
  int buffer_len = buffer->length();
  // EXPECT_CALL(*client_write_buffer_, write(_))
  //     .WillOnce(Invoke(client_write_buffer_, &MockWatermarkBuffer::failWrite));
  EXPECT_CALL(*client_write_buffer_, move(_));
  client_write_buffer_->move(*buffer);

  {
    // Go from watermarks being off to being above the high watermark.
    EXPECT_CALL(client_callbacks_, onAboveWriteBufferHighWatermark());
    EXPECT_CALL(client_callbacks_, onBelowWriteBufferLowWatermark()).Times(0);
    client_connection_->setBufferLimits(buffer_len - 3);
    ASSERT_TRUE(client_connection_->aboveHighWatermark());
  }

  {
    // Go from above the high watermark to in between both.
    EXPECT_CALL(client_callbacks_, onAboveWriteBufferHighWatermark()).Times(0);
    EXPECT_CALL(client_callbacks_, onBelowWriteBufferLowWatermark()).Times(0);
    client_connection_->setBufferLimits(buffer_len + 1);
    ASSERT_TRUE(client_connection_->aboveHighWatermark());
  }

  {
    // Go from above the high watermark to below the low watermark.
    EXPECT_CALL(client_callbacks_, onAboveWriteBufferHighWatermark()).Times(0);
    EXPECT_CALL(client_callbacks_, onBelowWriteBufferLowWatermark());
    client_connection_->setBufferLimits(buffer_len * 3);
    ASSERT_FALSE(client_connection_->aboveHighWatermark());
  }

  {
    // Go back in between and verify neither callback is called.
    EXPECT_CALL(client_callbacks_, onAboveWriteBufferHighWatermark()).Times(0);
    EXPECT_CALL(client_callbacks_, onBelowWriteBufferLowWatermark()).Times(0);
    client_connection_->setBufferLimits(buffer_len * 2);
    ASSERT_FALSE(client_connection_->aboveHighWatermark());
  }
  EXPECT_CALL(*client_write_buffer_, drain(_));
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  client_connection_->close(ConnectionCloseType::NoFlush);
}

// Test that as watermark levels are changed, the appropriate callbacks are triggered.
TEST_P(PipeConnectionImplTest, ReadWatermarksWhenFilterConsume) {

  setupPipe();
  doConnect();

  client_connection_->setBufferLimits(2);
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::Connected));

  auto client_read_filter = std::make_shared<NiceMock<MockReadFilter>>();
  client_connection_->addReadFilter(client_read_filter);
  Buffer::WatermarkBuffer& client_read_buffer =
      dynamic_cast<Buffer::WatermarkBuffer&>(client_connection_->getReadBuffer().buffer);

  auto on_filter_data_exit = [&](Buffer::Instance&, bool) -> FilterStatus {
    dispatcher_->exit();
    return FilterStatus::StopIteration;
  };

  ASSERT_FALSE(client_read_buffer.highWatermarkTriggered());
  ASSERT_TRUE(client_connection_->readEnabled());
  // Add 4 bytes to the buffer and verify the connection becomes read disabled.
  {
    ENVOY_LOG_MISC(debug, "lambdai: adding 4 bytes, expecting read stalled.");
    EXPECT_CALL(*client_read_filter, onData(_, false)).WillOnce(Invoke(on_filter_data_exit));
    Buffer::OwnedImpl buffer("data");
    server_connection_->write(buffer, false);
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    ASSERT_TRUE(dynamic_cast<Buffer::WatermarkBuffer&>(client_connection_->getReadBuffer().buffer)
                    .highWatermarkTriggered());
    ASSERT_FALSE(client_connection_->readEnabled());
  }

  // Drain 3 bytes from the buffer. This bring sit below the low watermark, and
  // read enables, as well as triggering a kick for the remaining byte.
  {
    ENVOY_LOG_MISC(debug, "lambdai: draining 3, expecting read resume");
    client_connection_->getReadBuffer().buffer.drain(3);
    ASSERT_FALSE(dynamic_cast<Buffer::WatermarkBuffer&>(client_connection_->getReadBuffer().buffer)
                     .highWatermarkTriggered());
    ASSERT_TRUE(client_connection_->readEnabled());

    EXPECT_CALL(*client_read_filter, onData(_, false));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  client_connection_->close(ConnectionCloseType::NoFlush);
}

TEST_P(PipeConnectionImplTest, ReadWatermarksWhenFilterDisabledRead) {

  setupPipe();
  doConnect();

  client_connection_->setBufferLimits(2);
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::Connected));

  auto client_read_filter = std::make_shared<NiceMock<MockReadFilter>>();
  client_connection_->addReadFilter(client_read_filter);
  Buffer::WatermarkBuffer& client_read_buffer =
      dynamic_cast<Buffer::WatermarkBuffer&>(client_connection_->getReadBuffer().buffer);

  auto on_filter_data_exit = [&](Buffer::Instance&, bool) -> FilterStatus {
    dispatcher_->exit();
    return FilterStatus::StopIteration;
  };

  ASSERT_FALSE(client_read_buffer.highWatermarkTriggered());
  ASSERT_TRUE(client_connection_->readEnabled());

  // Add 5 bytes to the buffer and verify the connection becomes read disabled
  // again.
  {
    ENVOY_LOG_MISC(debug, "lambdai: write 5");
    Buffer::OwnedImpl buffer("hello");
    EXPECT_CALL(*client_read_filter, onData(_, false)).WillOnce(Invoke(on_filter_data_exit));
    server_connection_->write(buffer, false);
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    ASSERT_TRUE(dynamic_cast<Buffer::WatermarkBuffer&>(client_connection_->getReadBuffer().buffer)
                    .highWatermarkTriggered());
    ASSERT_FALSE(client_connection_->readEnabled());
  }

  // Now have the consumer read disable.
  // This time when the buffer is drained, there will be no kick as the consumer
  // does not want to read.
  {
    ENVOY_LOG_MISC(debug, "lambdai: explicitly disable and drain to low watermark");

    client_connection_->readDisable(true);
    client_connection_->getReadBuffer().buffer.drain(4);
    ASSERT_FALSE(dynamic_cast<Buffer::WatermarkBuffer&>(client_connection_->getReadBuffer().buffer)
                     .highWatermarkTriggered());
    ASSERT_FALSE(client_connection_->readEnabled());

    EXPECT_CALL(*client_read_filter, onData(_, false)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Now read enable again.
  // Inside the onData call, readDisable and readEnable. This should trigger
  // another kick on the next dispatcher loop, so onData gets called twice.
  {
    ENVOY_LOG_MISC(debug, "lambdai: explicitly enable but doesn't consume at the first time");

    client_connection_->readDisable(false);
    EXPECT_CALL(*client_read_filter, onData(_, false))
        .Times(2)
        .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
          client_connection_->readDisable(true);
          client_connection_->readDisable(false);
          return FilterStatus::StopIteration;
        }))
        .WillRepeatedly(Invoke(on_filter_data_exit));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  client_connection_->close(ConnectionCloseType::NoFlush);
}

TEST_P(PipeConnectionImplTest, ReadWatermarksConnectionUnconsumedBufferWhenConsumerWantsToRead) {

  setupPipe();
  doConnect();

  client_connection_->setBufferLimits(2);
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::Connected));

  auto client_read_filter = std::make_shared<NiceMock<MockReadFilter>>();
  client_connection_->addReadFilter(client_read_filter);
  Buffer::WatermarkBuffer& client_read_buffer =
      dynamic_cast<Buffer::WatermarkBuffer&>(client_connection_->getReadBuffer().buffer);

  auto on_filter_data_exit = [&](Buffer::Instance&, bool) -> FilterStatus {
    dispatcher_->exit();
    return FilterStatus::StopIteration;
  };

  ASSERT_FALSE(client_read_buffer.highWatermarkTriggered());
  ASSERT_TRUE(client_connection_->readEnabled());

  // Test the same logic for dispatched_buffered_data from the
  // onReadReady() (read_disable_count_ != 0) path.
  {
    // Fill the buffer and verify the socket is read disabled.
    Buffer::OwnedImpl buffer("hello");
    EXPECT_CALL(*client_read_filter, onData(_, false))
        .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
          dispatcher_->exit();
          return FilterStatus::StopIteration;
        }));
    server_connection_->write(buffer, false);

    dispatcher_->run(Event::Dispatcher::RunType::Block);
    ASSERT_TRUE(dynamic_cast<Buffer::WatermarkBuffer&>(client_connection_->getReadBuffer().buffer)
                    .highWatermarkTriggered());
    ASSERT_FALSE(client_connection_->readEnabled());

    // Read disable and read enable, to set dispatch_buffered_data_ true.
    client_connection_->readDisable(true);
    client_connection_->readDisable(false);
    // Now event loop. This hits the early on-Read path. As above, read
    // disable and read enable from inside the stack of onData, to ensure that
    // dispatch_buffered_data_ works correctly.
    EXPECT_CALL(*client_read_filter, onData(_, false))
        .Times(2)
        .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
          client_connection_->readDisable(true);
          client_connection_->readDisable(false);
          return FilterStatus::StopIteration;
        }))
        .WillRepeatedly(Invoke(on_filter_data_exit));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  client_connection_->close(ConnectionCloseType::NoFlush);
}

// Write some data to the connection. It will automatically attempt to flush
// it to the upstream file descriptor via a write() call to buffer_, which is
// configured to succeed and accept all bytes read.
TEST_P(PipeConnectionImplTest, BasicWrite) {
  useMockBuffer();
  setupPipe();
  doConnect();

  EXPECT_FALSE(client_connection_->aboveHighWatermark());
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::Connected));

  // Send the data to the connection and verify it is sent upstream.
  std::string data_to_write = "hello world";
  Buffer::OwnedImpl buffer_to_write(data_to_write);
  std::string data_written;
  EXPECT_CALL(*client_write_buffer_, move(_))
      .WillRepeatedly(DoAll(AddBufferToStringWithoutDraining(&data_written),
                            Invoke(client_write_buffer_, &MockWatermarkBuffer::baseMove)));
  // Buffer source socket does not write to io handle.
  EXPECT_CALL(*client_write_buffer_, write(_)).Times(0);
  EXPECT_CALL(*client_write_buffer_, drain(_))
      .WillOnce(Invoke(client_write_buffer_, &MockWatermarkBuffer::trackDrains));
  client_connection_->write(buffer_to_write, false);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(data_to_write, data_written);

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  client_connection_->close(ConnectionCloseType::NoFlush);
}

// Similar to BasicWrite, only with watermarks set.
TEST_P(PipeConnectionImplTest, WriteAllAndClearHighwatermark) {
  useMockBuffer();
  setupPipe();
  doConnect();
  client_connection_->setBufferLimits(2);
  EXPECT_FALSE(client_connection_->aboveHighWatermark());
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::Connected));

  std::string data_to_write = "hello world";
  Buffer::OwnedImpl first_buffer_to_write(data_to_write);
  std::string data_written;
  EXPECT_CALL(*client_write_buffer_, move(_))
      .WillRepeatedly(DoAll(AddBufferToStringWithoutDraining(&data_written),
                            Invoke(client_write_buffer_, &MockWatermarkBuffer::baseMove)));
  // Buffer source socket does not write to io handle.
  EXPECT_CALL(*client_write_buffer_, write(_)).Times(0);
  EXPECT_CALL(*client_write_buffer_, drain(_))
      .WillOnce(Invoke(client_write_buffer_, &MockWatermarkBuffer::trackDrains));
  // The write() call on the connection will buffer enough data to bring the connection above the
  // high watermark but the subsequent drain immediately brings it back below.
  // A nice future performance optimization would be to latch if the socket is writable in the
  // connection_impl, and try an immediate drain inside of write() to avoid thrashing here.
  EXPECT_CALL(client_callbacks_, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(client_callbacks_, onBelowWriteBufferLowWatermark());
  client_connection_->write(first_buffer_to_write, false);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(data_to_write, data_written);

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  client_connection_->close(ConnectionCloseType::NoFlush);
}

TEST_P(PipeConnectionImplTest, WritePartialAndHighwatermarkRemains) {
  useMockBuffer();
  setupPipe();
  doConnect();
  client_connection_->setBufferLimits(2);

  server_socket_->read_buffer_.setWatermarks(2);
  ASSERT_FALSE(server_socket_->isOverHighWatermark());
  server_socket_->read_buffer_.add("buffered_data");
  ASSERT_TRUE(server_socket_->isOverHighWatermark());
  ASSERT_EQ(server_socket_->read_buffer_.length(), 13);

  ASSERT_FALSE(client_connection_->aboveHighWatermark());
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::Connected));

  // Now do the write again, but this time the d
  const std::string data_to_write = "hello world";
  std::string data_written;
  Buffer::OwnedImpl second_buffer_to_write(data_to_write);
  EXPECT_CALL(*client_write_buffer_, move(_))
      .WillRepeatedly(DoAll(AddBufferToStringWithoutDraining(&data_written),
                            Invoke(client_write_buffer_, &MockWatermarkBuffer::baseMove)));
  EXPECT_CALL(*client_write_buffer_, write(_)).Times(0);
  // The write() call on the connection will buffer enough data to bring the connection abovethe
  // high watermark and as the data will not flush it should not return below the watermark.
  EXPECT_CALL(client_callbacks_, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(client_callbacks_, onBelowWriteBufferLowWatermark()).Times(0);
  client_connection_->write(second_buffer_to_write, false);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // No data is written to server side buffer.
  ASSERT_EQ(server_socket_->read_buffer_.length(), 13);

  // Now we drain the server side buffer and it will accept bytes from client.
  EXPECT_CALL(client_callbacks_, onBelowWriteBufferLowWatermark()).Times(1);
  server_socket_->read_buffer_.drain(13);

  // Clean up the connection. The close() (called via disconnect) will attempt to flush. The
  // call to write() will succeed, bringing the connection back under the low watermark.
  EXPECT_CALL(*client_write_buffer_, write(_)).Times(0);

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  client_connection_->close(ConnectionCloseType::FlushWrite);
}

// // Read and write random bytes and ensure we don't encounter issues.
// TEST_P(PipeConnectionImplTest, WatermarkFuzzing) {
//   useMockBuffer();
//   setUpBasicConnection();

//   connect();
//   client_connection_->setBufferLimits(10);

//   TestRandomGenerator rand;
//   int bytes_buffered = 0;
//   int new_bytes_buffered = 0;

//   bool is_below = true;
//   bool is_above = false;

//   ON_CALL(*client_write_buffer_, write(_))
//       .WillByDefault(testing::Invoke(client_write_buffer_, &MockWatermarkBuffer::failWrite));
//   ON_CALL(*client_write_buffer_, drain(_))
//       .WillByDefault(testing::Invoke(client_write_buffer_, &MockWatermarkBuffer::baseDrain));
//   EXPECT_CALL(*client_write_buffer_, drain(_)).Times(AnyNumber());

//   // Randomly write 1-20 bytes and read 1-30 bytes per loop.
//   for (int i = 0; i < 50; ++i) {
//     // The bytes to read this loop.
//     int bytes_to_write = rand.random() % 20 + 1;
//     // The bytes buffered at the beginning of this loop.
//     bytes_buffered = new_bytes_buffered;
//     // Bytes to flush upstream.
//     int bytes_to_flush = std::min<int>(rand.random() % 30 + 1, bytes_to_write +
//     bytes_buffered);
//     // The number of bytes buffered at the end of this loop.
//     new_bytes_buffered = bytes_buffered + bytes_to_write - bytes_to_flush;
//     ENVOY_LOG_MISC(trace,
//                    "Loop iteration {} bytes_to_write {} bytes_to_flush {} bytes_buffered is {}
//                    and " "will be be {}", i, bytes_to_write, bytes_to_flush, bytes_buffered,
//                    new_bytes_buffered);

//     std::string data(bytes_to_write, 'a');
//     Buffer::OwnedImpl buffer_to_write(data);

//     // If the current bytes buffered plus the bytes we write this loop go over
//     // the watermark and we're not currently above, we will get a callback for
//     // going above.
//     if (bytes_to_write + bytes_buffered > 11 && is_below) {
//       ENVOY_LOG_MISC(trace, "Expect onAboveWriteBufferHighWatermark");
//       EXPECT_CALL(client_callbacks_, onAboveWriteBufferHighWatermark());
//       is_below = false;
//       is_above = true;
//     }
//     // If after the bytes are flushed upstream the number of bytes remaining is
//     // below the low watermark and the bytes were not previously below the low
//     // watermark, expect the callback for going below.
//     if (new_bytes_buffered <= 5 && is_above) {
//       ENVOY_LOG_MISC(trace, "Expect onBelowWriteBufferLowWatermark");
//       EXPECT_CALL(client_callbacks_, onBelowWriteBufferLowWatermark());
//       is_below = true;
//       is_above = false;
//     }

//     // Do the actual work. Write |buffer_to_write| bytes to the connection and
//     // drain |bytes_to_flush| before having the buffer failWrite()
//     EXPECT_CALL(*client_write_buffer_, move(_))
//         .WillOnce(Invoke(client_write_buffer_, &MockWatermarkBuffer::baseMove));
//     EXPECT_CALL(*client_write_buffer_, write(_))
//         .WillOnce(
//             DoAll(Invoke([&](IoHandle&) -> void { client_write_buffer_->drain(bytes_to_flush);
//             }),
//                   Return(testing::ByMove(Api::IoCallUint64Result(
//                       bytes_to_flush, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}))))))
//         .WillRepeatedly(testing::Invoke(client_write_buffer_,
//         &MockWatermarkBuffer::failWrite));
//     client_connection_->write(buffer_to_write, false);
//     dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
//   }

//   EXPECT_CALL(client_callbacks_, onBelowWriteBufferLowWatermark()).Times(AnyNumber());
//   disconnect(true);
// }

INSTANTIATE_TEST_SUITE_P(IpVersions, PipeConnectionImplTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

} // namespace
} // namespace Network
} // namespace Envoy