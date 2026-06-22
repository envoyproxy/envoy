#include <sys/socket.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "source/common/io/io_uring_worker_impl.h"
#include "source/common/network/address_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/io/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "absl/strings/string_view.h"
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
      : IoUringWorkerImpl(std::move(io_uring_instance), 8192, 1000, 131072, 16384, dispatcher) {}

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

// When the completion queue still holds entries after a reap batch, the worker re-arms the read
// event so the remaining completions are drained on the next loop iteration.
TEST(IoUringWorkerImplTest, OnFileEventReArmsWhenCompletionsRemain) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  Event::FileReadyCb file_event_callback;
  auto* file_event = new NiceMock<Event::MockFileEvent>();

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
      .WillOnce(DoAll(SaveArg<1>(&file_event_callback), Return(file_event)));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  EXPECT_CALL(mock_io_uring, forEveryCompletion(_));
  EXPECT_CALL(mock_io_uring, submit());
  EXPECT_CALL(mock_io_uring, hasReadyCompletions()).WillOnce(Return(true)).RetiresOnSaturation();
  EXPECT_CALL(*file_event, activate(Event::FileReadyType::Read));
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
}

// When the completion queue is drained after a reap batch, the worker does not re-arm the read
// event.
TEST(IoUringWorkerImplTest, OnFileEventDoesNotReArmWhenCompletionsDrained) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  Event::FileReadyCb file_event_callback;
  auto* file_event = new NiceMock<Event::MockFileEvent>();

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
      .WillOnce(DoAll(SaveArg<1>(&file_event_callback), Return(file_event)));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  // hasReadyCompletions() returns false by default, so the read event must not be re-armed.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_));
  EXPECT_CALL(mock_io_uring, submit());
  EXPECT_CALL(*file_event, activate(_)).Times(0);
  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

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
      0, 131072, 16384, true);
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
      0, 131072, 16384, true);

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
      0, worker, [](uint32_t) { return absl::OkStatus(); }, 0, 131072, 16384, false);

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
      0, worker, [](uint32_t) { return absl::OkStatus(); }, 0, 131072, 16384, false);

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
  IoUringClientSocket socket(
      0, worker, [](uint32_t) { return absl::OkStatus(); }, 0, 131072, 16384, false);

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

// Once the write buffer is above the high watermark the socket refuses further writes so the
// handler keeps the data and backpressure propagates.
TEST(IoUringWorkerImplTest, WriteAppliesBackpressureAboveHighWatermark) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
      .WillOnce(ReturnNew<NiceMock<Event::MockFileEvent>>());
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  // Use small watermarks (high=64, low=16) for easy testing.
  IoUringServerSocket socket(
      0, worker, [](uint32_t) { return absl::OkStatus(); }, 0, 64, 16, false);

  // The first write of 100 bytes is accepted and pushes the buffer above the high watermark.
  Request* write_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareWritev(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&write_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  Buffer::OwnedImpl buf;
  buf.add(std::string(100, 'x'));
  socket.write(buf);
  EXPECT_EQ(0, buf.length());

  // Further writes are refused while above the high watermark. The slice write reports zero bytes
  // accepted and the buffer write keeps the data, and neither submits a new request.
  std::string more(50, 'y');
  Buffer::RawSlice slice;
  slice.mem_ = more.data();
  slice.len_ = more.size();
  EXPECT_EQ(0, socket.write(&slice, 1));

  Buffer::OwnedImpl buf2;
  buf2.add(std::string(50, 'z'));
  socket.write(buf2);
  EXPECT_EQ(50, buf2.length());

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  delete write_req;
}

// When the write buffer drains below the low watermark the socket injects a write event to resume
// the handler.
TEST(IoUringWorkerImplTest, WriteResumesBelowLowWatermark) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
      .WillOnce(ReturnNew<NiceMock<Event::MockFileEvent>>());
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  IoUringServerSocket socket(
      0, worker, [](uint32_t) { return absl::OkStatus(); }, 0, 64, 16, false);

  // Write 100 bytes to move above the high watermark.
  Request* write_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareWritev(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&write_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  Buffer::OwnedImpl buf;
  buf.add(std::string(100, 'x'));
  socket.write(buf);

  // Draining 90 bytes leaves 10 bytes (below the low watermark), so a write event is injected to
  // resume the handler and the remaining bytes are submitted.
  Request* injected_req = nullptr;
  EXPECT_CALL(mock_io_uring, injectCompletion(0, _, -EAGAIN)).WillOnce(SaveArg<1>(&injected_req));
  Request* write_req2 = nullptr;
  EXPECT_CALL(mock_io_uring, prepareWritev(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&write_req2), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  socket.onWrite(write_req, 90, false);

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  delete write_req;
  delete write_req2;
  delete injected_req;
}

// A failed write drains the buffer and clears backpressure, so a later write before close is
// accepted instead of being silently refused by the stale high watermark.
TEST(IoUringWorkerImplTest, WriteFailureClearsBackpressure) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
      .WillOnce(ReturnNew<NiceMock<Event::MockFileEvent>>());
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  IoUringServerSocket socket(
      0, worker, [](uint32_t) { return absl::OkStatus(); }, 0, 64, 16, false);

  // Write 100 bytes to move above the high watermark.
  Request* write_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareWritev(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&write_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  Buffer::OwnedImpl buf;
  buf.add(std::string(100, 'x'));
  socket.write(buf);

  // The write fails, which drains the buffer and clears backpressure.
  socket.onWrite(write_req, -ECONNRESET, false);

  // A later write is accepted in full and submits a new request.
  Request* write_req2 = nullptr;
  EXPECT_CALL(mock_io_uring, prepareWritev(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&write_req2), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  std::string data(50, 'y');
  Buffer::RawSlice slice;
  slice.mem_ = data.data();
  slice.len_ = data.size();
  EXPECT_EQ(50, socket.write(&slice, 1));

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  delete write_req;
  delete write_req2;
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
  IoUringClientSocket socket(
      0, worker, [](uint32_t) { return absl::OkStatus(); }, 0, 131072, 16384, false);

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

class ReproSocket : public IoUringSocketEntry {
public:
  ReproSocket(os_fd_t fd, IoUringWorkerImpl& parent)
      : IoUringSocketEntry(fd, parent, [](uint32_t) { return absl::OkStatus(); }, false) {}

  void onRead(Request* req, int32_t result, bool injected) override {
    IoUringSocketEntry::onRead(req, result, injected);
    if (injected) {
      cleanup();
    }
  }

  void write(Buffer::Instance&) override {}
  uint64_t write(const Buffer::RawSlice*, uint64_t) override { return 0; }
  void shutdown(int) override {}
};

class IoUringWorkerRepro : public IoUringWorkerImpl {
public:
  using IoUringWorkerImpl::IoUringWorkerImpl;
  IoUringSocket& addReproSocket(os_fd_t fd) {
    return addSocket(std::make_unique<ReproSocket>(fd, *this));
  }
};

TEST(IoUringWorkerImplTest, DoubleFreeOnSocketCloseDuringInjectedCompletion) {
  if (!isIoUringSupported()) {
    GTEST_SKIP() << "io_uring not supported on this kernel";
  }

  Event::MockDispatcher dispatcher;
  auto io_uring = std::make_unique<IoUringImpl>(64, false, false, 0);
  Event::FileReadyCb file_event_callback;
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, _, _))
      .WillOnce(testing::DoAll(testing::SaveArg<1>(&file_event_callback),
                               testing::ReturnNew<testing::NiceMock<Event::MockFileEvent>>()));

  IoUringWorkerRepro worker(std::move(io_uring), 8192, 1000, 131072, 16384, dispatcher);
  os_fd_t fd = 1;
  auto& socket = worker.addReproSocket(fd);

  worker.injectCompletion(socket, Request::RequestType::Read, 0);

  EXPECT_CALL(dispatcher, deferredDelete_(testing::_));
  file_event_callback(Event::FileReadyType::Read).IgnoreError();

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
}

// A fake provided buffer pool backed by plain heap memory. It records every buffer handed back so a
// test can assert that consumed buffers are recycled.
class FakeIoUringBufferPool : public IoUringBufferPool {
public:
  FakeIoUringBufferPool(uint32_t buffer_size, uint32_t buffer_count)
      : buffer_size_(buffer_size), memory_(static_cast<size_t>(buffer_size) * buffer_count, 0) {}

  uint8_t* getBuffer(uint32_t buffer_id) override {
    return memory_.data() + static_cast<size_t>(buffer_id) * buffer_size_;
  }
  uint32_t bufferSize() const override { return buffer_size_; }
  void releaseBuffer(const void* buffer) override {
    released_buffers_.push_back(static_cast<uint32_t>(
        (static_cast<const uint8_t*>(buffer) - memory_.data()) / buffer_size_));
  }

  void fill(uint32_t buffer_id, absl::string_view data) {
    memcpy(getBuffer(buffer_id), data.data(), data.size());
  }

  const uint32_t buffer_size_;
  std::vector<uint8_t> memory_;
  std::vector<uint32_t> released_buffers_;
};

// A `multishot` read stays armed across completions, delivers data in the provided buffers, and
// recycles each buffer once the read buffer is drained.
TEST(IoUringWorkerImplTest, ServerSocketMultishotRead) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  auto buffer_pool = std::make_shared<FakeIoUringBufferPool>(16, 4);
  buffer_pool->fill(0, "hello");
  buffer_pool->fill(1, "world!");

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(mock_io_uring, isMultishotEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_io_uring, bufferPool()).WillRepeatedly(Return(buffer_pool));
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  IoUringServerSocket* socket_ptr = nullptr;
  std::string read_data;
  int32_t last_result = 1;
  auto read_cb = [&socket_ptr, &read_data, &last_result](uint32_t events) -> absl::Status {
    if (events & Event::FileReadyType::Read) {
      const ReadParam& param = socket_ptr->getReadParam().ref();
      last_result = param.result_;
      if (param.buf_.length() > 0) {
        read_data.append(param.buf_.toString());
        param.buf_.drain(param.buf_.length());
      }
    }
    return absl::OkStatus();
  };

  // Enabling read arms a `multishot` receive rather than a single readv.
  Request* read_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadMultishot(_, _))
      .WillOnce(DoAll(SaveArg<1>(&read_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  IoUringServerSocket socket(0, worker, read_cb, 0, 131072, 16384, false);
  socket_ptr = &socket;
  socket.enableRead();

  // The first completion delivers data in provided buffer 0 and stays armed for more.
  read_req->setBufferId(0);
  read_req->setMoreCompletions(true);
  socket.onRead(read_req, 5, false);
  EXPECT_EQ("hello", read_data);
  EXPECT_EQ(std::vector<uint32_t>({0}), buffer_pool->released_buffers_);

  // A second completion reuses the same armed request with another provided buffer.
  read_req->setBufferId(1);
  read_req->setMoreCompletions(true);
  socket.onRead(read_req, 6, false);
  EXPECT_EQ("helloworld!", read_data);
  EXPECT_EQ(std::vector<uint32_t>({0, 1}), buffer_pool->released_buffers_);

  // A terminal completion without more data releases the armed request and reports the close.
  read_req->setMoreCompletions(false);
  socket.onRead(read_req, 0, false);
  EXPECT_EQ(0, last_result);

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  delete read_req;
}

// A `multishot` completion that reports data with no provided buffer id is flagged as a bug and
// the data is dropped instead of indexing the pool out of bounds.
TEST(IoUringWorkerImplTest, ServerSocketMultishotReadInvalidBufferIdDropsData) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  auto buffer_pool = std::make_shared<FakeIoUringBufferPool>(16, 4);

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(mock_io_uring, isMultishotEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_io_uring, bufferPool()).WillRepeatedly(Return(buffer_pool));
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  // Arm the `multishot` read.
  Request* read_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadMultishot(_, _))
      .WillOnce(DoAll(SaveArg<1>(&read_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  IoUringServerSocket socket(
      0, worker, [](uint32_t) { return absl::OkStatus(); }, 0, 131072, 16384, false);
  socket.enableRead();

  // The completion reports data with an absent buffer id, which is dropped and recycles no buffer.
  read_req->setBufferId(-1);
  read_req->setMoreCompletions(true);
  EXPECT_ENVOY_BUG(socket.onRead(read_req, 5, false), "invalid multishot buffer id");
  EXPECT_EQ(0, socket.getReadBuffer().length());
  EXPECT_TRUE(buffer_pool->released_buffers_.empty());

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  delete read_req;
}

// A buffer pool exhaustion ends the `multishot` read and falls back to a readv, which re-arms a
// `multishot` read on its next successful completion.
TEST(IoUringWorkerImplTest, ServerSocketMultishotEnobufsFallback) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  auto buffer_pool = std::make_shared<FakeIoUringBufferPool>(16, 4);

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(mock_io_uring, isMultishotEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_io_uring, bufferPool()).WillRepeatedly(Return(buffer_pool));
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  // Arm the `multishot` read.
  Request* multishot_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadMultishot(_, _))
      .WillOnce(DoAll(SaveArg<1>(&multishot_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  IoUringServerSocket socket(
      0, worker, [](uint32_t) { return absl::OkStatus(); }, 0, 131072, 16384, false);
  socket.enableRead();

  // A pool exhaustion ends the `multishot` and the next read falls back to a readv.
  Request* readv_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadv(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&readv_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  multishot_req->setMoreCompletions(false);
  socket.onRead(multishot_req, -ENOBUFS, false);

  // A successful readv completion re-arms the `multishot` read for the next read.
  Request* rearm_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadMultishot(_, _))
      .WillOnce(DoAll(SaveArg<1>(&rearm_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  socket.onRead(readv_req, 4, false);

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  delete multishot_req;
  delete readv_req;
  delete rearm_req;
}

// The readv path grows the next read size when a read fills the buffer, caps the growth at the
// configured multiple and resets to the base size when a read does not fill the buffer.
TEST(IoUringWorkerImplTest, ServerSocketAdaptiveReadSize) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);
  const uint32_t base = worker.readBufferSize();

  IoUringServerSocket* socket_ptr = nullptr;
  auto read_cb = [&socket_ptr](uint32_t events) -> absl::Status {
    if (events & Event::FileReadyType::Read) {
      const ReadParam& param = socket_ptr->getReadParam().ref();
      param.buf_.drain(param.buf_.length());
    }
    return absl::OkStatus();
  };

  // Collect every readv request so its buffer size can be inspected.
  std::vector<Request*> reqs;
  EXPECT_CALL(mock_io_uring, prepareReadv(_, _, _, _, _))
      .WillRepeatedly(DoAll(Invoke([&reqs](os_fd_t, const struct iovec*, unsigned, off_t,
                                           Request* req) { reqs.push_back(req); }),
                            Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(testing::AnyNumber());

  IoUringServerSocket socket(0, worker, read_cb, 0, 131072, 16384, false);
  socket_ptr = &socket;

  // The first read starts at the base size.
  socket.enableRead();
  ASSERT_EQ(1, reqs.size());
  EXPECT_EQ(base, static_cast<ReadRequest*>(reqs.back())->iov_.iov_len);

  // A read that fills the buffer doubles the next read size up to the configured multiple.
  uint32_t expected = base;
  for (size_t i = 0; i < 5; i++) {
    const uint32_t filled = static_cast<ReadRequest*>(reqs.back())->iov_.iov_len;
    socket.onRead(reqs.back(), static_cast<int32_t>(filled), false);
    expected = std::min(expected * 2, base * 16);
    ASSERT_EQ(i + 2, reqs.size());
    EXPECT_EQ(expected, static_cast<ReadRequest*>(reqs.back())->iov_.iov_len);
  }
  EXPECT_EQ(base * 16, expected);

  // A read that does not fill the buffer resets the next read size to the base.
  socket.onRead(reqs.back(), 1, false);
  EXPECT_EQ(base, static_cast<ReadRequest*>(reqs.back())->iov_.iov_len);

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  for (Request* req : reqs) {
    delete req;
  }
}

// A kernel that registers the buffer ring but rejects `multishot` recv with the given error
// disables `multishot` for the socket, which then uses readv for every read.
void multishotDisabledOnUnsupportedKernel(int32_t reject_error) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  auto buffer_pool = std::make_shared<FakeIoUringBufferPool>(16, 4);

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(mock_io_uring, isMultishotEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_io_uring, bufferPool()).WillRepeatedly(Return(buffer_pool));
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  // Arm the `multishot` read.
  Request* multishot_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadMultishot(_, _))
      .WillOnce(DoAll(SaveArg<1>(&multishot_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  IoUringServerSocket socket(
      0, worker, [](uint32_t) { return absl::OkStatus(); }, 0, 131072, 16384, false);
  socket.enableRead();

  // The kernel rejects `multishot` recv, so the next read falls back to a readv.
  Request* readv_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadv(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&readv_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  multishot_req->setMoreCompletions(false);
  socket.onRead(multishot_req, reject_error, false);

  // A successful readv does not re-arm `multishot` since it is permanently disabled.
  Request* next_readv_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadv(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&next_readv_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  socket.onRead(readv_req, 4, false);

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  delete multishot_req;
  delete readv_req;
  delete next_readv_req;
}

TEST(IoUringWorkerImplTest, ServerSocketMultishotDisabledOnEinval) {
  multishotDisabledOnUnsupportedKernel(-EINVAL);
}

TEST(IoUringWorkerImplTest, ServerSocketMultishotDisabledOnEopnotsupp) {
  multishotDisabledOnUnsupportedKernel(-EOPNOTSUPP);
}

// A `multishot` completion that arrives after the socket is closed without keeping the fd open
// drops the data but still releases the provided buffer so the pool is not depleted.
TEST(IoUringWorkerImplTest, ServerSocketMultishotCloseDiscardsAndReleasesBuffer) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  auto buffer_pool = std::make_shared<FakeIoUringBufferPool>(16, 4);
  buffer_pool->fill(2, "dropped");

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(mock_io_uring, isMultishotEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_io_uring, bufferPool()).WillRepeatedly(Return(buffer_pool));
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  // Arm the `multishot` read.
  Request* multishot_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadMultishot(_, _))
      .WillOnce(DoAll(SaveArg<1>(&multishot_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  auto& socket = worker.addServerSocket(0, [](uint32_t) { return absl::OkStatus(); }, false);

  // Close the socket and cancel the armed read.
  Request* cancel_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareCancel(_, _))
      .WillOnce(DoAll(SaveArg<1>(&cancel_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  socket.close(false, nullptr);
  socket.onCancel(cancel_req, 0, false);

  // The terminal `multishot` completion drops its data but releases the buffer, then closes.
  Request* close_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareClose(_, _))
      .WillOnce(DoAll(SaveArg<1>(&close_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  multishot_req->setBufferId(2);
  multishot_req->setMoreCompletions(false);
  socket.onRead(multishot_req, 7, false);
  EXPECT_EQ(std::vector<uint32_t>({2}), buffer_pool->released_buffers_);

  // The close completion cleans up the socket.
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(0));
  EXPECT_CALL(dispatcher, deferredDelete_);
  socket.onClose(close_req, 0, false);

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  delete multishot_req;
  delete cancel_req;
  delete close_req;
}

// Closing a `multishot` socket while keeping the fd open hands the buffered data to the callback as
// owned memory so the provided buffers are not released to the ring from another worker thread.
TEST(IoUringWorkerImplTest, ServerSocketMultishotCloseKeepFdOpenCopiesData) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  auto buffer_pool = std::make_shared<FakeIoUringBufferPool>(16, 4);
  buffer_pool->fill(0, "hello");

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(mock_io_uring, isMultishotEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_io_uring, bufferPool()).WillRepeatedly(Return(buffer_pool));
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  // Arm the `multishot` read.
  Request* multishot_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadMultishot(_, _))
      .WillOnce(DoAll(SaveArg<1>(&multishot_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  auto& socket = worker.addServerSocket(0, [](uint32_t) { return absl::OkStatus(); }, false);

  // Close the socket but keep the fd open for migration, which cancels the armed read.
  Request* cancel_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareCancel(_, _))
      .WillOnce(DoAll(SaveArg<1>(&cancel_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  std::string closed_data;
  bool is_closed = false;
  socket.close(true, [&buffer_pool, &closed_data, &is_closed](Buffer::Instance& read_buffer) {
    closed_data = read_buffer.toString();
    // The provided buffer is drained and released on this thread before the callback runs, so the
    // callback only receives owned data.
    EXPECT_EQ(std::vector<uint32_t>({0}), buffer_pool->released_buffers_);
    read_buffer.drain(read_buffer.length());
    is_closed = true;
  });
  socket.onCancel(cancel_req, 0, false);

  // The terminal `multishot` completion delivers data, which is copied to the callback before
  // cleanup.
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(0));
  EXPECT_CALL(dispatcher, deferredDelete_);
  multishot_req->setBufferId(0);
  multishot_req->setMoreCompletions(false);
  socket.onRead(multishot_req, 5, false);
  EXPECT_TRUE(is_closed);
  EXPECT_EQ("hello", closed_data);

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  delete multishot_req;
  delete cancel_req;
}

} // namespace
} // namespace Io
} // namespace Envoy
