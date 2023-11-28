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
      : IoUringSocketEntry(
            fd, parent, [](uint32_t) {}, false) {}
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
  file_event_callback(Event::FileReadyType::Read);

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
  auto& io_uring_socket = worker.addServerSocket(
      fd, [](uint32_t) {}, false);

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
  file_event_callback(Event::FileReadyType::Read);

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
  file_event_callback(Event::FileReadyType::Read);

  // After the close request finished, the socket will be cleanup.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&close_req](const CompletionCb& cb) { cb(close_req, 0, false); }));
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  file_event_callback(Event::FileReadyType::Read);

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
  auto& io_uring_socket = worker.addServerSocket(
      fd, [](uint32_t) {}, false);

  // Fake an injected completion.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&io_uring_socket](const CompletionCb& cb) {
        auto* req = new Request(Request::RequestType::Write, io_uring_socket);
        cb(req, -EAGAIN, true);
      }));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  file_event_callback(Event::FileReadyType::Read);

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
  file_event_callback(Event::FileReadyType::Read);

  // After the close request finished, the socket will be cleanup.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&close_req](const CompletionCb& cb) { cb(close_req, 0, false); }));
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  file_event_callback(Event::FileReadyType::Read);

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
  worker->addServerSocket(
      fd, [](uint32_t) {}, false);

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
  auto& io_uring_socket = worker.addServerSocket(
      fd, [](uint32_t) {}, false);

  // Disable the socket, then there will be no new read request.
  io_uring_socket.disableRead();
  // Fake the read request finish.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&read_req](const CompletionCb& cb) { cb(read_req, -EAGAIN, false); }));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  file_event_callback(Event::FileReadyType::Read);

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
  file_event_callback(Event::FileReadyType::Read);

  // After the close request finished, the socket will be cleanup.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&close_req](const CompletionCb& cb) { cb(close_req, 0, false); }));
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  file_event_callback(Event::FileReadyType::Read);

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
      0, worker, [](uint32_t events) { EXPECT_EQ(events, Event::FileReadyType::Closed); }, 0, true);
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
      0, worker, [](uint32_t events) { EXPECT_EQ(events, Event::FileReadyType::Closed); }, 0, true);

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
  IoUringServerSocket socket(
      0, worker, [](uint32_t) {}, 0, false);

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
  IoUringServerSocket socket(
      0, worker, [](uint32_t) {}, 0, false);

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

TEST(IoUringWorkerImplTest, CloseKeepFDOpenAndSaveData) {
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
      0, [](uint32_t events) { EXPECT_EQ(events, Event::FileReadyType::Closed); }, false);

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

} // namespace
} // namespace Io
} // namespace Envoy
