#include <sys/socket.h>

#include "source/common/io/io_uring_worker_impl.h"
#include "source/common/network/address_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/io/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::DoAll;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNew;
using testing::SaveArg;

namespace Envoy {
namespace Io {
namespace {

class IoUringSocketTestImpl : public IoUringSocketEntry {
public:
  IoUringSocketTestImpl(os_fd_t fd, IoUringWorkerImpl& parent)
      : IoUringSocketEntry(fd, parent, [](uint32_t) { return absl::OkStatus(); }, false) {}
  void cleanupForTest() { cleanup(); }

  void write(Buffer::Instance&) override {}
  uint64_t write(const Buffer::RawSlice*, uint64_t) override { return 0; }
  void shutdown(int) override {}
};

class IoUringWorkerTestImpl : public IoUringWorkerImpl {
public:
  IoUringWorkerTestImpl(IoUringPtr io_uring_instance, Event::Dispatcher& dispatcher)
      : IoUringWorkerImpl(std::move(io_uring_instance), 8192, 1000, dispatcher) {}

  IoUringSocket& addTestSocket(os_fd_t fd) {
    return addSocket(std::make_unique<IoUringSocketTestImpl>(fd, *this));
  }

  const std::list<IoUringSocketEntryPtr>& getSockets() const { return sockets_; }

  void submitForTest() { submit(); }
};

// TODO (soulxu): This is only for test coverage, we suppose to have correct
// implementation to handle the request submit failed.
TEST(IoUringWorkerImplTest, SubmitRequestsFailed) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  os_fd_t fd;
  SET_SOCKET_INVALID(fd);
  auto& io_uring_socket = worker.addTestSocket(fd);

  EXPECT_CALL(mock_io_uring, prepareReadv(fd, _, _, _, _))
      .WillOnce(Return<IoUringResult>(IoUringResult::Ok))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, prepareReadv(fd, _, _, _, _))
      .WillOnce(Return<IoUringResult>(IoUringResult::Failed))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  delete worker.submitReadRequest(io_uring_socket);

  Buffer::OwnedImpl buf;
  auto slices = buf.getRawSlices();
  EXPECT_CALL(mock_io_uring, prepareWritev(fd, _, _, _, _))
      .WillOnce(Return<IoUringResult>(IoUringResult::Ok))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, prepareWritev(fd, _, _, _, _))
      .WillOnce(Return<IoUringResult>(IoUringResult::Failed))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  delete worker.submitWriteRequest(io_uring_socket, slices);

  EXPECT_CALL(mock_io_uring, prepareCancel(_, _))
      .WillOnce(Return<IoUringResult>(IoUringResult::Ok))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, prepareCancel(_, _))
      .WillOnce(Return<IoUringResult>(IoUringResult::Failed))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  delete worker.submitCancelRequest(io_uring_socket, nullptr);

  EXPECT_CALL(mock_io_uring, prepareClose(fd, _))
      .WillOnce(Return<IoUringResult>(IoUringResult::Ok))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, prepareClose(fd, _))
      .WillOnce(Return<IoUringResult>(IoUringResult::Failed))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  delete worker.submitCloseRequest(io_uring_socket);

  EXPECT_CALL(mock_io_uring, prepareShutdown(fd, _, _))
      .WillOnce(Return<IoUringResult>(IoUringResult::Ok))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, prepareShutdown(fd, _, _))
      .WillOnce(Return<IoUringResult>(IoUringResult::Failed))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  delete worker.submitShutdownRequest(io_uring_socket, SHUT_WR);

  EXPECT_EQ(fd, io_uring_socket.fd());
  EXPECT_EQ(1, worker.getSockets().size());
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  dynamic_cast<IoUringSocketTestImpl*>(worker.getSockets().front().get())->cleanupForTest();
  EXPECT_EQ(0, worker.getNumOfSockets());
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
}

TEST(IoUringWorkerImplTest, CleanupSocket) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  os_fd_t fd = 11;
  SET_SOCKET_INVALID(fd);
  auto& io_uring_socket = worker.addTestSocket(fd);

  EXPECT_EQ(fd, io_uring_socket.fd());
  EXPECT_EQ(1, worker.getSockets().size());
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  dynamic_cast<IoUringSocketTestImpl*>(worker.getSockets().front().get())->cleanupForTest();
  EXPECT_EQ(0, worker.getNumOfSockets());
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
}

TEST(IoUringWorkerImplTest, DelaySubmit) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  Event::FileReadyCb file_event_callback;

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
      .WillOnce(
          DoAll(SaveArg<1>(&file_event_callback), ReturnNew<NiceMock<Event::MockFileEvent>>()));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  os_fd_t fd = 11;
  SET_SOCKET_INVALID(fd);
  worker.addTestSocket(fd);

  // The submit only be invoked one time.
  EXPECT_CALL(mock_io_uring, submit());
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_)).WillOnce(Invoke([&worker](CompletionCb) {
    // Emulate multiple submit.
    worker.submitForTest();
    worker.submitForTest();
  }));
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  dynamic_cast<IoUringSocketTestImpl*>(worker.getSockets().front().get())->cleanupForTest();
  EXPECT_EQ(0, worker.getNumOfSockets());
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
}

// This tests ensure the write request won't be override by an injected completion.
TEST(IoUringWorkerImplTest, ServerSocketInjectAfterWrite) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  Event::FileReadyCb file_event_callback;

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
      .WillOnce(
          DoAll(SaveArg<1>(&file_event_callback), ReturnNew<NiceMock<Event::MockFileEvent>>()));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  os_fd_t fd = 11;
  SET_SOCKET_INVALID(fd);

  Request* read_req = nullptr;
  // The read request added by server socket constructor.
  EXPECT_CALL(mock_io_uring, prepareReadv(fd, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&read_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  auto& io_uring_socket =
      worker.addServerSocket(fd, [](uint32_t) { return absl::OkStatus(); }, false);

  // Add a write request.
  std::string data = "Hello";
  Buffer::OwnedImpl buf1;
  buf1.add(data);
  Request* write_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareWritev(fd, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&write_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  io_uring_socket.write(buf1);

  // Fake an injected completion.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&io_uring_socket](const CompletionCb& cb) {
        auto* req = new Request(Request::RequestType::Write, io_uring_socket);
        cb(req, -EAGAIN, true);
      }));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  Buffer::OwnedImpl buf2;
  buf2.add(data);

  // Add another write request to ensure the incomplete request is still
  // there, so the new write request won't be submitted.
  EXPECT_CALL(mock_io_uring, prepareWritev(fd, _, _, _, _)).Times(0);
  EXPECT_CALL(mock_io_uring, submit()).Times(0).RetiresOnSaturation();
  io_uring_socket.write(buf2);

  // Start the close process.
  Request* cancel_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareCancel(_, _))
      .WillOnce(DoAll(SaveArg<1>(&cancel_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  EXPECT_CALL(dispatcher, createTimer_(_)).WillOnce(ReturnNew<NiceMock<Event::MockTimer>>());
  io_uring_socket.close(false);

  // Finish the read, cancel and write request, then expect the close request submitted.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&read_req, &cancel_req, &write_req](const CompletionCb& cb) {
        cb(read_req, -EAGAIN, false);
        cb(cancel_req, 0, false);
        cb(write_req, -EAGAIN, false);
      }));
  Request* close_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareClose(_, _))
      .WillOnce(DoAll(SaveArg<1>(&close_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  // After the close request finished, the socket will be cleanup.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&close_req](const CompletionCb& cb) { cb(close_req, 0, false); }));
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  EXPECT_EQ(0, worker.getSockets().size());
}

// This tests ensure the read request won't be override by an injected completion.
TEST(IoUringWorkerImplTest, ServerSocketInjectAfterRead) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  Event::FileReadyCb file_event_callback;

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
      .WillOnce(
          DoAll(SaveArg<1>(&file_event_callback), ReturnNew<NiceMock<Event::MockFileEvent>>()));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  os_fd_t fd = 11;
  SET_SOCKET_INVALID(fd);

  // The read request added by server socket constructor.
  Request* read_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadv(fd, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&read_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  auto& io_uring_socket =
      worker.addServerSocket(fd, [](uint32_t) { return absl::OkStatus(); }, false);

  // Fake an injected completion.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&io_uring_socket](const CompletionCb& cb) {
        auto* req = new Request(Request::RequestType::Write, io_uring_socket);
        cb(req, -EAGAIN, true);
      }));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  // When close the socket, expect there still have a incomplete read
  // request, so it has to cancel the request first.
  Request* cancel_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareCancel(_, _))
      .WillOnce(DoAll(SaveArg<1>(&cancel_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  io_uring_socket.close(false);

  // Finish the read and cancel request, then expect the close request submitted.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&read_req, &cancel_req](const CompletionCb& cb) {
        cb(read_req, -EAGAIN, false);
        cb(cancel_req, 0, false);
      }));
  Request* close_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareClose(_, _))
      .WillOnce(DoAll(SaveArg<1>(&close_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  // After the close request finished, the socket will be cleanup.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&close_req](const CompletionCb& cb) { cb(close_req, 0, false); }));
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  EXPECT_EQ(0, worker.getSockets().size());
}

TEST(IoUringWorkerImplTest, CloseAllSocketsWhenDestruction) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  Event::FileReadyCb file_event_callback;

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
      .WillOnce(
          DoAll(SaveArg<1>(&file_event_callback), ReturnNew<NiceMock<Event::MockFileEvent>>()));

  std::unique_ptr<IoUringWorkerTestImpl> worker =
      std::make_unique<IoUringWorkerTestImpl>(std::move(io_uring_instance), dispatcher);

  os_fd_t fd = 11;
  SET_SOCKET_INVALID(fd);

  // The read request added by server socket constructor.
  Request* read_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadv(fd, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&read_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  worker->addServerSocket(fd, [](uint32_t) { return absl::OkStatus(); }, false);

  // The IoUringWorker will close all the existing sockets.
  Request* cancel_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareCancel(_, _))
      .WillOnce(DoAll(SaveArg<1>(&cancel_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();

  // The IoUringWorker will wait for the socket closed.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&mock_io_uring, fd, &read_req, &cancel_req](const CompletionCb& cb) {
        // When the cancel request is done, the close request will be submitted.
        Request* close_req = nullptr;
        EXPECT_CALL(mock_io_uring, prepareClose(fd, _))
            .WillOnce(DoAll(SaveArg<1>(&close_req), Return<IoUringResult>(IoUringResult::Ok)))
            .RetiresOnSaturation();
        EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();

        EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));

        // Fake the read request cancel completion.
        cb(read_req, -ECANCELED, false);

        // Fake the cancel request is done.
        cb(cancel_req, 0, false);

        // Fake the close request is done.
        cb(close_req, 0, false);
      }));

  EXPECT_CALL(dispatcher, deferredDelete_);
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  worker.reset();
}

TEST(IoUringWorkerImplTest, ServerCloseWithWriteRequestOnly) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  Event::FileReadyCb file_event_callback;

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
      .WillOnce(
          DoAll(SaveArg<1>(&file_event_callback), ReturnNew<NiceMock<Event::MockFileEvent>>()));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  os_fd_t fd = 11;
  SET_SOCKET_INVALID(fd);

  // The read request added by server socket constructor.
  Request* read_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadv(fd, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&read_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  auto& io_uring_socket =
      worker.addServerSocket(fd, [](uint32_t) { return absl::OkStatus(); }, false);

  // Disable the socket, then there will be no new read request.
  io_uring_socket.disableRead();
  // Fake the read request finish.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&read_req](const CompletionCb& cb) { cb(read_req, -EAGAIN, false); }));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  Request* write_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareWritev(fd, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&write_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  Buffer::OwnedImpl write_buf;
  write_buf.add("Hello");
  io_uring_socket.write(write_buf);

  // Close the socket, but there is no read request, so cancel request won't
  // be submitted.
  EXPECT_CALL(dispatcher, createTimer_(_)).WillOnce(ReturnNew<NiceMock<Event::MockTimer>>());
  io_uring_socket.close(false);

  Request* close_req = nullptr;
  // Finish the read and cancel request, then expect the close request submitted.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&mock_io_uring, &write_req, &close_req](const CompletionCb& cb) {
        EXPECT_CALL(mock_io_uring, prepareClose(_, _))
            .WillOnce(DoAll(SaveArg<1>(&close_req), Return<IoUringResult>(IoUringResult::Ok)))
            .RetiresOnSaturation();
        EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();

        cb(write_req, -EAGAIN, false);
      }));
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  // After the close request finished, the socket will be cleanup.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&close_req](const CompletionCb& cb) { cb(close_req, 0, false); }));
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  EXPECT_EQ(0, worker.getSockets().size());
}

// Make sure that even the socket is disabled, that remote close can be handled.
TEST(IoUringWorkerImplTest, CloseDetected) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  Request* read_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadv(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&read_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  IoUringServerSocket socket(
      0, worker,
      [](uint32_t events) {
        EXPECT_EQ(events, Event::FileReadyType::Closed);
        return absl::OkStatus();
      },
      0, true);
  socket.enableRead();
  socket.disableRead();

  // Consumes the first read request.
  Request* read_req2 = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadv(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&read_req2), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  socket.onRead(read_req, 1, false);
  socket.onRead(nullptr, 0, false);

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  delete read_req;
  delete read_req2;
}

TEST(IoUringWorkerImplTest, AvoidDuplicatedCloseRequest) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);
  IoUringServerSocket socket(
      0, worker,
      [](uint32_t events) {
        EXPECT_EQ(events, Event::FileReadyType::Closed);
        return absl::OkStatus();
      },
      0, true);

  Request* close_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareClose(_, _))
      .WillOnce(DoAll(SaveArg<1>(&close_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();

  socket.close(false);
  socket.close(false);

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  delete close_req;
}

TEST(IoUringWorkerImplTest, NoOnWriteCallingBackInShutdownWriteSocketInjection) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);
  IoUringServerSocket socket(0, worker, [](uint32_t) { return absl::OkStatus(); }, 0, false);

  // Shutdown and then shutdown completes.
  EXPECT_CALL(mock_io_uring, submit());
  Request* shutdown_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareShutdown(socket.fd(), _, _))
      .WillOnce(DoAll(SaveArg<2>(&shutdown_req), Return<IoUringResult>(IoUringResult::Ok)));
  socket.shutdown(SHUT_WR);
  socket.onShutdown(shutdown_req, 0, false);

  // onWrite happens after the shutdown completed will not trigger calling back.
  socket.onWrite(nullptr, 0, true);

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  delete shutdown_req;
}

TEST(IoUringWorkerImplTest, NoOnWriteCallingBackInCloseAfterShutdownWriteSocketInjection) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);
  IoUringServerSocket socket(0, worker, [](uint32_t) { return absl::OkStatus(); }, 0, false);

  // Shutdown and then close.
  EXPECT_CALL(mock_io_uring, submit());
  Request* shutdown_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareShutdown(socket.fd(), _, _))
      .WillOnce(DoAll(SaveArg<2>(&shutdown_req), Return<IoUringResult>(IoUringResult::Ok)));
  socket.shutdown(SHUT_WR);
  Request* close_req = nullptr;
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  socket.close(false);

  // onWrite happens after the close after shutdown will not trigger calling back.
  socket.onWrite(nullptr, 0, true);

  delete shutdown_req;
  delete close_req;
}

TEST(IoUringWorkerImplTest, CloseKeepFdOpenAndSaveData) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  // The read request submitted.
  Request* read_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadv(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&read_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  auto& socket = worker.addServerSocket(
      0,
      [](uint32_t events) {
        EXPECT_EQ(events, Event::FileReadyType::Closed);
        return absl::OkStatus();
      },
      false);

  // Close the socket, but keep the fd open.
  Request* cancel_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareCancel(_, _))
      .WillOnce(DoAll(SaveArg<1>(&cancel_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  Buffer::OwnedImpl buffer;
  bool is_closed = false;
  socket.close(true, [&is_closed](Buffer::Instance& read_buffer) {
    // Expect the data is saved.
    EXPECT_EQ(1, read_buffer.length());
    is_closed = true;
  });

  // consumes the cancel request.
  socket.onCancel(cancel_req, 0, false);

  // Consumes the read request.
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(0));
  EXPECT_CALL(dispatcher, deferredDelete_);
  socket.onRead(read_req, 1, false);
  EXPECT_TRUE(is_closed);

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  delete read_req;
  delete cancel_req;
}

TEST(IoUringWorkerImplTest, NoOnConnectCallingBackInClosing) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);
  IoUringClientSocket socket(0, worker, [](uint32_t) { return absl::OkStatus(); }, 0, false);

  auto addr = std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0");
  EXPECT_CALL(mock_io_uring, submit()).Times(3);
  void* connect_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareConnect(socket.fd(), _, _))
      .WillOnce(DoAll(SaveArg<2>(&connect_req), Return<IoUringResult>(IoUringResult::Ok)));
  socket.connect(addr);
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  void* cancel_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareCancel(_, _))
      .WillOnce(DoAll(SaveArg<1>(&cancel_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  void* close_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareClose(socket.fd(), _))
      .WillOnce(DoAll(SaveArg<1>(&close_req), Return<IoUringResult>(IoUringResult::Ok)));
  socket.close(false);
  socket.onCancel(static_cast<Request*>(cancel_req), 0, false);
  socket.onConnect(nullptr, 0, false);
  delete static_cast<Request*>(connect_req);
  delete static_cast<Request*>(cancel_req);
  delete static_cast<Request*>(close_req);
}

TEST(IoUringWorkerImplTest, NoEnableReadOnConnectError) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  Event::FileReadyCb file_event_callback;
  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
      .WillOnce(
          DoAll(SaveArg<1>(&file_event_callback), ReturnNew<NiceMock<Event::MockFileEvent>>()));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);
  IoUringClientSocket socket(0, worker, [](uint32_t) { return absl::OkStatus(); }, 0, false);

  auto addr = std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0");
  EXPECT_CALL(mock_io_uring, submit());
  void* connect_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareConnect(socket.fd(), _, _))
      .WillOnce(DoAll(SaveArg<2>(&connect_req), Return<IoUringResult>(IoUringResult::Ok)));
  socket.connect(addr);
  // The socket stays in Initialized status if connect failed.
  EXPECT_CALL(mock_io_uring, injectCompletion(_, _, _))
      .WillOnce(Invoke([](os_fd_t, Request* req, int32_t) { delete req; }));
  socket.onConnect(nullptr, 1, false);
  EXPECT_EQ(socket.getStatus(), Initialized);

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  delete static_cast<Request*>(connect_req);
}

TEST(IoUringWorkerImplTest, SubmitSendRecvRequestsFailed) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  os_fd_t fd;
  SET_SOCKET_INVALID(fd);
  auto& io_uring_socket = worker.addTestSocket(fd);

  // Test submitSendRequest
  std::string test_data = "Hello, World!";
  EXPECT_CALL(mock_io_uring, prepareSend(fd, _, test_data.length(), 0, _))
      .WillOnce(Return<IoUringResult>(IoUringResult::Ok))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, prepareSend(fd, _, test_data.length(), 0, _))
      .WillOnce(Return<IoUringResult>(IoUringResult::Failed))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  delete worker.submitSendRequest(io_uring_socket, test_data.c_str(), test_data.length(), 0);

  // Test submitRecvRequest
  char recv_buffer[256];
  EXPECT_CALL(mock_io_uring, prepareRecv(fd, _, sizeof(recv_buffer), 0, _))
      .WillOnce(Return<IoUringResult>(IoUringResult::Ok))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, prepareRecv(fd, _, sizeof(recv_buffer), 0, _))
      .WillOnce(Return<IoUringResult>(IoUringResult::Failed))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  delete worker.submitRecvRequest(io_uring_socket, recv_buffer, sizeof(recv_buffer), 0);

  EXPECT_EQ(fd, io_uring_socket.fd());
  EXPECT_EQ(1, worker.getSockets().size());
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  dynamic_cast<IoUringSocketTestImpl*>(worker.getSockets().front().get())->cleanupForTest();
  EXPECT_EQ(0, worker.getNumOfSockets());
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
}

TEST(IoUringWorkerImplTest, SubmitSendmsgRecvmsgRequestsFailed) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  os_fd_t fd;
  SET_SOCKET_INVALID(fd);
  auto& io_uring_socket = worker.addTestSocket(fd);

  // Setup sendmsg structure
  std::string test_data = "Hello, Sendmsg!";
  struct iovec send_iov;
  send_iov.iov_base = const_cast<char*>(test_data.c_str());
  send_iov.iov_len = test_data.length();

  struct msghdr send_msg;
  memset(&send_msg, 0, sizeof(send_msg));
  send_msg.msg_iov = &send_iov;
  send_msg.msg_iovlen = 1;

  // Test submitSendmsgRequest
  EXPECT_CALL(mock_io_uring, prepareSendmsg(fd, _, 0, _))
      .WillOnce(Return<IoUringResult>(IoUringResult::Ok))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, prepareSendmsg(fd, _, 0, _))
      .WillOnce(Return<IoUringResult>(IoUringResult::Failed))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  delete worker.submitSendmsgRequest(io_uring_socket, &send_msg, 0);

  // Setup recvmsg structure
  char recv_buffer[256];
  struct iovec recv_iov;
  recv_iov.iov_base = recv_buffer;
  recv_iov.iov_len = sizeof(recv_buffer);

  struct msghdr recv_msg;
  memset(&recv_msg, 0, sizeof(recv_msg));
  recv_msg.msg_iov = &recv_iov;
  recv_msg.msg_iovlen = 1;

  // Test submitRecvmsgRequest
  EXPECT_CALL(mock_io_uring, prepareRecvmsg(fd, _, 0, _))
      .WillOnce(Return<IoUringResult>(IoUringResult::Ok))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, prepareRecvmsg(fd, _, 0, _))
      .WillOnce(Return<IoUringResult>(IoUringResult::Failed))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  delete worker.submitRecvmsgRequest(io_uring_socket, &recv_msg, 0);

  EXPECT_EQ(fd, io_uring_socket.fd());
  EXPECT_EQ(1, worker.getSockets().size());
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  dynamic_cast<IoUringSocketTestImpl*>(worker.getSockets().front().get())->cleanupForTest();
  EXPECT_EQ(0, worker.getNumOfSockets());
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
}

TEST(IoUringWorkerImplTest, ServerSocketSendRecvCompletion) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  Event::FileReadyCb file_event_callback;

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
      .WillOnce(
          DoAll(SaveArg<1>(&file_event_callback), ReturnNew<NiceMock<Event::MockFileEvent>>()));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  os_fd_t fd = 11;
  SET_SOCKET_INVALID(fd);

  Request* read_req = nullptr;
  // The read request added by server socket constructor.
  EXPECT_CALL(mock_io_uring, prepareReadv(fd, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&read_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  auto& io_uring_socket =
      worker.addServerSocket(fd, [](uint32_t) { return absl::OkStatus(); }, false);

  // Test send completion
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&io_uring_socket](const CompletionCb& cb) {
        // Create a SendRequest for testing
        std::string test_data = "Test data";
        auto* send_req = new SendRequest(io_uring_socket, test_data.c_str(), test_data.length(), 0);
        cb(send_req, test_data.length(), false);
      }));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  // Test recv completion
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&io_uring_socket](const CompletionCb& cb) {
        // Create a RecvRequest for testing
        auto* recv_req = new RecvRequest(io_uring_socket, 256, 0);
        // Fill the buffer with test data
        const char* test_data = "Received data";
        memcpy(recv_req->buf_.get(), test_data, strlen(test_data));
        cb(recv_req, strlen(test_data), false);
      }));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  // Cleanup
  Request* cancel_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareCancel(_, _))
      .WillOnce(DoAll(SaveArg<1>(&cancel_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  io_uring_socket.close(false);

  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&read_req, &cancel_req](const CompletionCb& cb) {
        cb(read_req, -ECANCELED, false);
        cb(cancel_req, 0, false);
      }));
  Request* close_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareClose(_, _))
      .WillOnce(DoAll(SaveArg<1>(&close_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&close_req](const CompletionCb& cb) { cb(close_req, 0, false); }));
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  EXPECT_EQ(0, worker.getSockets().size());
}

TEST(IoUringWorkerImplTest, ServerSocketSendmsgRecvmsgCompletion) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  Event::FileReadyCb file_event_callback;

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
      .WillOnce(
          DoAll(SaveArg<1>(&file_event_callback), ReturnNew<NiceMock<Event::MockFileEvent>>()));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  os_fd_t fd = 11;
  SET_SOCKET_INVALID(fd);

  Request* read_req = nullptr;
  // The read request added by server socket constructor.
  EXPECT_CALL(mock_io_uring, prepareReadv(fd, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&read_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  auto& io_uring_socket =
      worker.addServerSocket(fd, [](uint32_t) { return absl::OkStatus(); }, false);

  // Test sendmsg completion
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&io_uring_socket](const CompletionCb& cb) {
        // Create a SendMsgRequest for testing
        std::string test_data = "Test sendmsg data";
        struct iovec iov;
        iov.iov_base = const_cast<char*>(test_data.c_str());
        iov.iov_len = test_data.length();

        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        auto* sendmsg_req = new SendMsgRequest(io_uring_socket, &msg, 0);
        cb(sendmsg_req, test_data.length(), false);
      }));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  // Test recvmsg completion
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&io_uring_socket](const CompletionCb& cb) {
        // Create a RecvMsgRequest for testing
        auto* recvmsg_req = new RecvMsgRequest(io_uring_socket, 256, 64, 0);
        // Fill the buffer with test data
        const char* test_data = "Received sendmsg data";
        memcpy(recvmsg_req->buf_.get(), test_data, strlen(test_data));
        cb(recvmsg_req, strlen(test_data), false);
      }));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  // Cleanup
  Request* cancel_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareCancel(_, _))
      .WillOnce(DoAll(SaveArg<1>(&cancel_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  io_uring_socket.close(false);

  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&read_req, &cancel_req](const CompletionCb& cb) {
        cb(read_req, -ECANCELED, false);
        cb(cancel_req, 0, false);
      }));
  Request* close_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareClose(_, _))
      .WillOnce(DoAll(SaveArg<1>(&close_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&close_req](const CompletionCb& cb) { cb(close_req, 0, false); }));
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  EXPECT_EQ(0, worker.getSockets().size());
}

// Test error handling in send/recv operations
TEST(IoUringWorkerImplTest, SendRecvErrorHandling) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  Event::FileReadyCb file_event_callback;

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
      .WillOnce(
          DoAll(SaveArg<1>(&file_event_callback), ReturnNew<NiceMock<Event::MockFileEvent>>()));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  os_fd_t fd = 11;
  SET_SOCKET_INVALID(fd);

  Request* read_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadv(fd, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&read_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  auto& io_uring_socket =
      worker.addServerSocket(fd, [](uint32_t) { return absl::OkStatus(); }, false);

  // Test send error completion (EPIPE)
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&io_uring_socket](const CompletionCb& cb) {
        std::string test_data = "Test data";
        auto* send_req = new SendRequest(io_uring_socket, test_data.c_str(), test_data.length(), 0);
        cb(send_req, -EPIPE, false);
      }));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  // Test recv error completion
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&io_uring_socket](const CompletionCb& cb) {
        auto* recv_req = new RecvRequest(io_uring_socket, 256, 0);
        cb(recv_req, -EAGAIN, false);
      }));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  // Cleanup
  Request* cancel_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareCancel(_, _))
      .WillOnce(DoAll(SaveArg<1>(&cancel_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  io_uring_socket.close(false);

  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&read_req, &cancel_req](const CompletionCb& cb) {
        cb(read_req, -ECANCELED, false);
        cb(cancel_req, 0, false);
      }));
  Request* close_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareClose(_, _))
      .WillOnce(DoAll(SaveArg<1>(&close_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&close_req](const CompletionCb& cb) { cb(close_req, 0, false); }));
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  EXPECT_EQ(0, worker.getSockets().size());
}

// Test request type validation in completion handling
TEST(IoUringWorkerImplTest, RequestTypeHandling) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  Event::FileReadyCb file_event_callback;

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
      .WillOnce(
          DoAll(SaveArg<1>(&file_event_callback), ReturnNew<NiceMock<Event::MockFileEvent>>()));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  os_fd_t fd = 11;
  SET_SOCKET_INVALID(fd);
  worker.addTestSocket(fd);

  // Test that all new request types are handled properly
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&worker](const CompletionCb& cb) {
        auto& socket = *worker.getSockets().front();

        // Test Send request type
        auto* send_req = new Request(Request::RequestType::Send, socket);
        cb(send_req, 10, false);

        // Test Recv request type
        auto* recv_req = new Request(Request::RequestType::Recv, socket);
        cb(recv_req, 15, false);

        // Test SendMsg request type
        auto* sendmsg_req = new Request(Request::RequestType::SendMsg, socket);
        cb(sendmsg_req, 20, false);

        // Test RecvMsg request type
        auto* recvmsg_req = new Request(Request::RequestType::RecvMsg, socket);
        cb(recvmsg_req, 25, false);
      }));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  dynamic_cast<IoUringSocketTestImpl*>(worker.getSockets().front().get())->cleanupForTest();
  EXPECT_EQ(0, worker.getNumOfSockets());
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
}

TEST(IoUringWorkerImplTest, ModeConfiguration) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));

  // Test default mode (ReadWritev).
  IoUringWorkerTestImpl worker_default(std::move(io_uring_instance), dispatcher);
  EXPECT_EQ(worker_default.getMode(), IoUringMode::ReadWritev);

  // Test SendRecv mode.
  io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring2 = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  EXPECT_CALL(mock_io_uring2, registerEventfd());
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerImpl worker_sendrecv(std::move(io_uring_instance), 8192, 1000, dispatcher,
                                    IoUringMode::SendRecv);
  EXPECT_EQ(worker_sendrecv.getMode(), IoUringMode::SendRecv);

  // Test SendmsgRecvmsg mode.
  io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring3 = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  EXPECT_CALL(mock_io_uring3, registerEventfd());
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerImpl worker_sendmsgrecvmsg(std::move(io_uring_instance), 8192, 1000, dispatcher,
                                          IoUringMode::SendmsgRecvmsg);
  EXPECT_EQ(worker_sendmsgrecvmsg.getMode(), IoUringMode::SendmsgRecvmsg);
}

TEST(IoUringWorkerImplTest, SendRecvModeWriteOperation) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  Event::FileReadyCb file_event_callback;

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
      .WillOnce(
          DoAll(SaveArg<1>(&file_event_callback), ReturnNew<NiceMock<Event::MockFileEvent>>()));

  // Create worker with SendRecv mode.
  IoUringWorkerImpl worker(std::move(io_uring_instance), 8192, 1000, dispatcher,
                           IoUringMode::SendRecv);

  os_fd_t fd = 11;
  SET_SOCKET_INVALID(fd);

  Request* read_req = nullptr;
  // The recv request should be used instead of readv.
  EXPECT_CALL(mock_io_uring, prepareRecv(fd, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&read_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  auto& io_uring_socket =
      worker.addServerSocket(fd, [](uint32_t) { return absl::OkStatus(); }, false);

  // Write operation should use send instead of writev.
  std::string data = "Hello SendRecv Mode!";
  Buffer::OwnedImpl buf;
  buf.add(data);
  Request* write_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareSend(fd, _, data.length(), MSG_NOSIGNAL, _))
      .WillOnce(DoAll(SaveArg<4>(&write_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  io_uring_socket.write(buf);

  // Verify that send operation completed successfully.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&write_req](const CompletionCb& cb) {
        cb(write_req, static_cast<int32_t>(20), false); // Simulate successful send.
      }));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  delete read_req;
}

TEST(IoUringWorkerImplTest, SendmsgRecvmsgModeWriteOperation) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  Event::FileReadyCb file_event_callback;

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
      .WillOnce(
          DoAll(SaveArg<1>(&file_event_callback), ReturnNew<NiceMock<Event::MockFileEvent>>()));

  // Create worker with SendmsgRecvmsg mode.
  IoUringWorkerImpl worker(std::move(io_uring_instance), 8192, 1000, dispatcher,
                           IoUringMode::SendmsgRecvmsg);

  os_fd_t fd = 11;
  SET_SOCKET_INVALID(fd);

  Request* read_req = nullptr;
  // The recvmsg request should be used instead of readv.
  EXPECT_CALL(mock_io_uring, prepareRecvmsg(fd, _, _, _))
      .WillOnce(DoAll(SaveArg<3>(&read_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  auto& io_uring_socket =
      worker.addServerSocket(fd, [](uint32_t) { return absl::OkStatus(); }, false);

  // Write operation should use sendmsg instead of writev.
  std::string data = "Hello SendmsgRecvmsg Mode!";
  Buffer::OwnedImpl buf;
  buf.add(data);
  Request* write_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareSendmsg(fd, _, MSG_NOSIGNAL, _))
      .WillOnce(DoAll(SaveArg<3>(&write_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  io_uring_socket.write(buf);

  // Verify that sendmsg operation completed successfully.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&write_req](const CompletionCb& cb) {
        cb(write_req, static_cast<int32_t>(25), false); // Simulate successful sendmsg.
      }));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  delete read_req;
}

TEST(IoUringWorkerImplTest, BackwardCompatibilityReadWritevMode) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  Event::FileReadyCb file_event_callback;

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
      .WillOnce(
          DoAll(SaveArg<1>(&file_event_callback), ReturnNew<NiceMock<Event::MockFileEvent>>()));

  // Create worker with explicit ReadWritev mode (should behave like before).
  IoUringWorkerImpl worker(std::move(io_uring_instance), 8192, 1000, dispatcher,
                           IoUringMode::ReadWritev);

  os_fd_t fd = 11;
  SET_SOCKET_INVALID(fd);

  Request* read_req = nullptr;
  // Should still use readv in ReadWritev mode.
  EXPECT_CALL(mock_io_uring, prepareReadv(fd, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&read_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  auto& io_uring_socket =
      worker.addServerSocket(fd, [](uint32_t) { return absl::OkStatus(); }, false);

  // Write operation should still use writev in ReadWritev mode.
  std::string data = "Backward Compatibility Test!";
  Buffer::OwnedImpl buf;
  buf.add(data);
  Request* write_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareWritev(fd, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&write_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  io_uring_socket.write(buf);

  // Verify that writev operation completed successfully.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&write_req](const CompletionCb& cb) {
        cb(write_req, static_cast<int32_t>(28), false); // Simulate successful writev.
      }));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  delete read_req;
}

} // namespace
} // namespace Io
} // namespace Envoy
