#include <sys/mman.h>
#include <sys/socket.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <tuple>
#include <vector>

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/io/io_uring_worker_impl.h"
#include "source/common/network/address_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/io/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"
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

class IoUringServerSocketLifecycleTestImpl : public IoUringServerSocket {
public:
  IoUringServerSocketLifecycleTestImpl(IoUringWorkerImpl& parent, Event::FileReadyCb cb)
      : IoUringServerSocket(0, parent, std::move(cb), 1000, 131072, 16384, false) {}
  void cleanupForTest() { cleanup(); }
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
  void processCompletionsForTest() { onFileEvent(); }

  IoUringServerSocketLifecycleTestImpl& addLifecycleSocket(Event::FileReadyCb cb) {
    return static_cast<IoUringServerSocketLifecycleTestImpl&>(
        addSocket(std::make_unique<IoUringServerSocketLifecycleTestImpl>(*this, std::move(cb))));
  }
};

class IoUringSubmitRetryTest : public testing::TestWithParam<bool> {};

TEST_P(IoUringSubmitRetryTest, RetainsRequestWhenSubmissionQueueIsFull) {
  testing::StrictMock<Event::MockDispatcher> dispatcher;
  auto io_uring_instance = std::make_unique<testing::StrictMock<MockIoUring>>();
  auto& mock_io_uring = *io_uring_instance;
  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  testing::StrictMock<MockIoUringSocket> socket;
  EXPECT_CALL(socket, fd()).WillRepeatedly(Return(11));
  const Network::Address::InstanceConstSharedPtr address =
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1");
  Request* first_attempt = nullptr;
  const bool multishot = GetParam();
  {
    testing::InSequence sequence;
    if (multishot) {
      EXPECT_CALL(mock_io_uring, prepareReadMultishot(11, _))
          .WillOnce(DoAll(SaveArg<1>(&first_attempt), Return(IoUringResult::Failed)));
    } else {
      EXPECT_CALL(mock_io_uring, prepareConnect(11, address, _))
          .WillOnce(DoAll(SaveArg<2>(&first_attempt), Return(IoUringResult::Failed)));
    }
    // Flush the full submission queue before retrying the same operation and request.
    EXPECT_CALL(mock_io_uring, submit()).WillOnce(Return(IoUringResult::Ok));
    if (multishot) {
      EXPECT_CALL(mock_io_uring, prepareReadMultishot(11, _))
          .WillOnce(Invoke([&](os_fd_t, Request* request) {
            EXPECT_EQ(first_attempt, request);
            return IoUringResult::Ok;
          }));
    } else {
      EXPECT_CALL(mock_io_uring, prepareConnect(11, address, _))
          .WillOnce(Invoke(
              [&](os_fd_t, const Network::Address::InstanceConstSharedPtr&, Request* request) {
                EXPECT_EQ(first_attempt, request);
                return IoUringResult::Ok;
              }));
    }
    EXPECT_CALL(mock_io_uring, submit()).WillOnce(Return(IoUringResult::Ok));
  }
  std::unique_ptr<Request> request(multishot ? worker.submitReadMultishotRequest(socket)
                                             : worker.submitConnectRequest(socket, address));
  ASSERT_NE(nullptr, request);
  EXPECT_EQ(first_attempt, request.get());
  EXPECT_EQ(multishot ? Request::RequestType::Read : Request::RequestType::Connect,
            request->type());
  EXPECT_EQ(&socket, &request->socket());
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
}

INSTANTIATE_TEST_SUITE_P(ConnectAndMultishot, IoUringSubmitRetryTest, testing::Values(false, true));

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
  ASSERT_OK(file_event_callback(Event::FileReadyType::Read));

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
  ASSERT_OK(file_event_callback(Event::FileReadyType::Read));

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
  ASSERT_OK(file_event_callback(Event::FileReadyType::Read));

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
  ASSERT_OK(file_event_callback(Event::FileReadyType::Read));

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
  ASSERT_OK(file_event_callback(Event::FileReadyType::Read));

  // After the close request finished, the socket will be cleanup.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&close_req](const CompletionCb& cb) { cb(close_req, 0, false); }));
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_OK(file_event_callback(Event::FileReadyType::Read));

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
  ASSERT_OK(file_event_callback(Event::FileReadyType::Read));

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
  ASSERT_OK(file_event_callback(Event::FileReadyType::Read));

  // After the close request finished, the socket will be cleanup.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&close_req](const CompletionCb& cb) { cb(close_req, 0, false); }));
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_OK(file_event_callback(Event::FileReadyType::Read));

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

class IoUringWorkerShutdownTest
    : public testing::TestWithParam<std::tuple<bool, bool, bool, bool>> {};

TEST_P(IoUringWorkerShutdownTest, CancelsPendingOperationWithoutRunningDispatcher) {
  const auto [already_closed, shutdown, already_canceling, partial_write] = GetParam();
  testing::StrictMock<Event::MockDispatcher> dispatcher;
  auto io_uring_instance = std::make_unique<testing::StrictMock<MockIoUring>>();
  MockIoUring& mock_io_uring = *io_uring_instance;
  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
      .WillOnce(ReturnNew<testing::StrictMock<Event::MockFileEvent>>());
  auto worker = std::make_unique<IoUringWorkerTestImpl>(std::move(io_uring_instance), dispatcher);

  // A client socket has no read request before connect completes, isolating the blocked operation.
  const os_fd_t fd = 11;
  auto& socket = worker->addClientSocket(fd, [](uint32_t) { return absl::OkStatus(); }, false);
  Request* pending_req = nullptr;
  if (shutdown) {
    EXPECT_CALL(mock_io_uring, prepareShutdown(fd, SHUT_WR, _))
        .WillOnce(DoAll(SaveArg<2>(&pending_req), Return(IoUringResult::Ok)));
    EXPECT_CALL(mock_io_uring, submit()).RetiresOnSaturation();
    socket.shutdown(SHUT_WR);
  } else {
    EXPECT_CALL(mock_io_uring, prepareWritev(fd, _, _, _, _))
        .WillOnce(DoAll(SaveArg<4>(&pending_req), Return(IoUringResult::Ok)));
    EXPECT_CALL(mock_io_uring, submit()).RetiresOnSaturation();
    Buffer::OwnedImpl data("blocked write");
    socket.write(data);
  }

  auto* timer = new testing::StrictMock<Event::MockTimer>();
  Event::TimerCb timeout_cb;
  EXPECT_CALL(dispatcher, createTimer_(_)).WillOnce(DoAll(SaveArg<0>(&timeout_cb), Return(timer)));
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(1000), _));
  EXPECT_CALL(*timer, disableTimer()).Times(2);

  Request* cancel_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareCancel(pending_req, _))
      .WillOnce(DoAll(SaveArg<1>(&cancel_req), Return(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).RetiresOnSaturation();
  if (already_closed) {
    socket.close(false);
    if (already_canceling) {
      timeout_cb();
    }
  }

  Request* close_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareClose(fd, _))
      .WillOnce(DoAll(SaveArg<1>(&close_req), Return(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).RetiresOnSaturation();
  Request* unexpected_write_req = nullptr;
  if (partial_write) {
    ON_CALL(mock_io_uring, prepareWritev(fd, _, _, _, _))
        .WillByDefault(DoAll(SaveArg<4>(&unexpected_write_req), Return(IoUringResult::Ok)));
    EXPECT_CALL(mock_io_uring, prepareWritev(fd, _, _, _, _)).Times(0);
  }
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_)).WillOnce(Invoke([&](const CompletionCb& cb) {
    // Fail deterministically on the original code, without hanging the test in its drain loop.
    EXPECT_NE(cancel_req, nullptr);
    if (partial_write && cancel_req != nullptr) {
      // The cancel completion can arrive before a positive completion that won the cancellation
      // race.
      cb(cancel_req, -ENOENT, false);
    }
    cb(pending_req, partial_write ? 1 : -ECANCELED, false);
    EXPECT_EQ(unexpected_write_req, nullptr);
    if (unexpected_write_req != nullptr) {
      // Drain a wrongly resubmitted request so the regression fails without hanging teardown.
      cb(unexpected_write_req, -ECANCELED, false);
    }
    if (!partial_write && cancel_req != nullptr) {
      cb(cancel_req, 0, false);
    }
    ASSERT_NE(close_req, nullptr);
    cb(close_req, 0, false);
  }));
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  worker.reset();
}

INSTANTIATE_TEST_SUITE_P(PendingWritesAndShutdowns, IoUringWorkerShutdownTest,
                         testing::Values(std::make_tuple(false, false, false, false),
                                         std::make_tuple(false, true, false, false),
                                         std::make_tuple(true, false, false, false),
                                         std::make_tuple(true, true, false, false),
                                         std::make_tuple(true, false, true, false),
                                         std::make_tuple(true, true, true, false),
                                         std::make_tuple(false, false, false, true),
                                         std::make_tuple(true, false, false, true),
                                         std::make_tuple(true, false, true, true)));

class IoUringCancelOrderTest
    : public testing::TestWithParam<std::tuple<std::array<uint32_t, 4>, bool>> {};

std::vector<std::array<uint32_t, 4>> cancellationCompletionOrders() {
  std::vector<std::array<uint32_t, 4>> orders;
  std::array<uint32_t, 4> order{0, 1, 2, 3};
  do {
    orders.push_back(order);
  } while (std::next_permutation(order.begin(), order.end()));
  return orders;
}

TEST_P(IoUringCancelOrderTest, WaitsForBothCancellationCompletionsBeforeClosing) {
  testing::StrictMock<Event::MockDispatcher> dispatcher;
  auto io_uring_instance = std::make_unique<testing::StrictMock<MockIoUring>>();
  MockIoUring& mock_io_uring = *io_uring_instance;
  Event::FileReadyCb file_event_callback;
  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
      .WillOnce(DoAll(SaveArg<1>(&file_event_callback),
                      ReturnNew<testing::StrictMock<Event::MockFileEvent>>()));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);
  const os_fd_t fd = 11;
  Request* read_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadv(fd, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&read_req), Return(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).RetiresOnSaturation();
  auto& socket = worker.addServerSocket(fd, [](uint32_t) { return absl::OkStatus(); }, false);
  Request* write_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareWritev(fd, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&write_req), Return(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).RetiresOnSaturation();
  Buffer::OwnedImpl data("blocked write");
  socket.write(data);

  Request* read_cancel_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareCancel(read_req, _))
      .WillOnce(DoAll(SaveArg<1>(&read_cancel_req), Return(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).RetiresOnSaturation();
  Event::TimerCb timeout_cb;
  EXPECT_CALL(dispatcher, createTimer_(_))
      .WillOnce(DoAll(SaveArg<0>(&timeout_cb), ReturnNew<NiceMock<Event::MockTimer>>()));
  socket.close(false);
  Request* write_cancel_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareCancel(write_req, _))
      .WillOnce(DoAll(SaveArg<1>(&write_cancel_req), Return(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).RetiresOnSaturation();
  timeout_cb();

  Request* close_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareClose(fd, _))
      .WillOnce(DoAll(SaveArg<1>(&close_req), Return(IoUringResult::Ok)));
  Request* unexpected_write_req = nullptr;
  ON_CALL(mock_io_uring, prepareWritev(fd, _, _, _, _))
      .WillByDefault(DoAll(SaveArg<4>(&unexpected_write_req), Return(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, prepareWritev(fd, _, _, _, _)).Times(0);
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_)).WillOnce(Invoke([&](const CompletionCb& cb) {
    const std::array<Request*, 4> requests{read_req, write_req, read_cancel_req, write_cancel_req};
    const auto& [order, partial_write] = GetParam();
    for (uint32_t i = 0; i < order.size(); ++i) {
      const uint32_t operation = order[i];
      const int32_t result = operation == 1 && partial_write ? 1 : operation < 2 ? -ECANCELED : 0;
      cb(requests[operation], result, false);
      if (i + 1 < order.size()) {
        EXPECT_EQ(close_req, nullptr);
      }
    }
    EXPECT_EQ(unexpected_write_req, nullptr);
    if (unexpected_write_req != nullptr) {
      cb(unexpected_write_req, -ECANCELED, false);
    }
    ASSERT_NE(close_req, nullptr);
    cb(close_req, 0, false);
  }));
  EXPECT_CALL(mock_io_uring, submit()).RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  ASSERT_OK(file_event_callback(Event::FileReadyType::Read));
  EXPECT_EQ(worker.getNumOfSockets(), 0);
}

INSTANTIATE_TEST_SUITE_P(CancellationOrders, IoUringCancelOrderTest,
                         testing::Combine(testing::ValuesIn(cancellationCompletionOrders()),
                                          testing::Bool()));

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
  ASSERT_OK(file_event_callback(Event::FileReadyType::Read));

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
  ASSERT_OK(file_event_callback(Event::FileReadyType::Read));

  // After the close request finished, the socket will be cleanup.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&close_req](const CompletionCb& cb) { cb(close_req, 0, false); }));
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  ASSERT_OK(file_event_callback(Event::FileReadyType::Read));

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

class IoUringReceiveBackpressureTest : public testing::Test {
public:
  void SetUp() override { initialize(); }

  void initialize() {
    buffer_pool_ = std::make_shared<FakeIoUringBufferPool>(64, 16);
    IoUringPtr io_uring;
    io_uring = std::make_unique<testing::StrictMock<MockIoUring>>();
    mock_ = static_cast<MockIoUring*>(io_uring.get());
    EXPECT_CALL(*mock_, isMultishotEnabled()).WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_, bufferPool()).WillRepeatedly(Return(buffer_pool_));
    EXPECT_CALL(*mock_, submit()).WillRepeatedly(Return(IoUringResult::Ok));
    EXPECT_CALL(dispatcher_, clearDeferredDeleteList()).Times(testing::AnyNumber());
    EXPECT_CALL(*mock_, registerEventfd());
    EXPECT_CALL(*mock_, removeInjectedCompletion(_)).Times(testing::AnyNumber());
    EXPECT_CALL(*mock_, hasReadyCompletions()).WillRepeatedly(Return(false));
    EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, Event::FileReadyType::Read))
        .WillOnce(ReturnNew<NiceMock<Event::MockFileEvent>>());
    EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, Event::FileReadyType::Closed))
        .Times(testing::AnyNumber())
        .WillRepeatedly(
            Invoke([this](os_fd_t, Event::FileReadyCb cb, Event::FileTriggerType, uint32_t) {
              auto* event = new NiceMock<Event::MockFileEvent>;
              watch_callbacks_.push_back(std::move(cb));
              watch_events_.push_back(event);
              return event;
            }));
    EXPECT_CALL(dispatcher_, deferredDelete_(_))
        .WillRepeatedly(Invoke([this](Event::DeferredDeletable*) { ++cleanups_; }));
    EXPECT_CALL(dispatcher_, createTimer_(_))
        .WillRepeatedly(
            DoAll(SaveArg<0>(&write_timeout_callback_), ReturnNew<NiceMock<Event::MockTimer>>()));
    EXPECT_CALL(*mock_, prepareReadMultishot(_, _))
        .WillRepeatedly(Invoke([this](os_fd_t, Request* request) {
          captureRead(request);
          return IoUringResult::Ok;
        }));
    EXPECT_CALL(*mock_, prepareReadv(_, _, _, _, _))
        .WillRepeatedly(Invoke([this](os_fd_t, const iovec*, unsigned, off_t, Request* request) {
          owned_requests_.emplace_back(request);
          readv_requests_.push_back(request);
          return IoUringResult::Ok;
        }));
    EXPECT_CALL(*mock_, prepareCancel(_, _))
        .WillRepeatedly(Invoke([this](Request* target, Request* cancellation) {
          owned_requests_.emplace_back(cancellation);
          cancellations_.emplace_back(target, cancellation);
          return IoUringResult::Ok;
        }));
    EXPECT_CALL(*mock_, prepareClose(_, _))
        .WillRepeatedly(Invoke([this](os_fd_t, Request* request) {
          owned_requests_.emplace_back(request);
          closes_.push_back(request);
          return IoUringResult::Ok;
        }));
    EXPECT_CALL(*mock_, prepareShutdown(_, _, _))
        .WillRepeatedly(Invoke([this](os_fd_t, int, Request* request) {
          owned_requests_.emplace_back(request);
          shutdowns_.push_back(request);
          return IoUringResult::Ok;
        }));
    EXPECT_CALL(*mock_, injectCompletion(_, _, _))
        .WillRepeatedly(Invoke([this](os_fd_t, Request* request, int32_t result) {
          EXPECT_EQ(-EAGAIN, result);
          owned_requests_.emplace_back(request);
          injected_.push_back(request);
        }));

    worker_ = std::make_unique<IoUringWorkerTestImpl>(std::move(io_uring), dispatcher_);
    socket_ = &worker_->addLifecycleSocket([this](uint32_t events) {
      events_.push_back(events);
      if (events & Event::FileReadyType::Read) {
        const ReadParam& param = socket_->getReadParam().ref();
        results_.push_back(param.result_);
        const uint64_t consumed = std::min(param.buf_.length(), max_read_bytes_);
        received_.append(param.buf_.toString(), 0, consumed);
        param.buf_.drain(consumed);
        if (param.result_ < 0 || pause_in_read_callback_) {
          socket_->disableRead();
        }
      }
      return absl::OkStatus();
    });
    socket_->enableRead();
    ASSERT_EQ(1, reads_.size());
  }

  void TearDown() override {
    if (worker_) {
      if (worker_->getNumOfSockets() != 0) {
        socket_->cleanupForTest();
      }
      worker_.reset();
    }
    owned_requests_.clear();
  }

  void captureRead(Request* request) {
    request->setMoreCompletions(true);
    owned_requests_.emplace_back(request);
    reads_.push_back(request);
  }

  void destroyRequest(Request* request) {
    for (auto& owned : owned_requests_) {
      if (owned.get() == request) {
        owned.reset();
        return;
      }
    }
    ADD_FAILURE() << "Request was not owned by this fixture";
  }

  void positive(Request* request, uint32_t slot, absl::string_view data, bool more = true) {
    buffer_pool_->fill(slot, data);
    request->setBufferId(slot);
    request->setMoreCompletions(more);
    socket_->onRead(request, data.size(), false);
    if (!more) {
      destroyRequest(request);
    }
  }

  void terminal(Request* request, int32_t result = -ECANCELED) {
    request->setMoreCompletions(false);
    socket_->onRead(request, result, false);
    destroyRequest(request);
  }

  void acknowledge(Request* request, int32_t result = 0) {
    socket_->onCancel(request, result, false);
    destroyRequest(request);
  }

  void deliverInjected() {
    unsigned callbacks = 0;
    while (!injected_.empty()) {
      ASSERT_LT(callbacks++, 16) << "resume injections failed to make progress";
      Request* request = injected_.front();
      injected_.erase(injected_.begin());
      socket_->onRead(request, -EAGAIN, true);
      destroyRequest(request);
    }
  }

  testing::StrictMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<FakeIoUringBufferPool> buffer_pool_;
  std::unique_ptr<IoUringWorkerTestImpl> worker_;
  MockIoUring* mock_{nullptr};
  IoUringServerSocketLifecycleTestImpl* socket_{nullptr};
  std::vector<std::unique_ptr<Request>> owned_requests_;
  std::vector<Request*> reads_, readv_requests_, injected_, closes_, shutdowns_;
  std::vector<std::pair<Request*, Request*>> cancellations_;
  std::vector<int32_t> results_;
  std::vector<uint32_t> events_;
  std::string received_;
  Event::TimerCb write_timeout_callback_;
  std::vector<Event::FileReadyCb> watch_callbacks_;
  std::vector<Event::MockFileEvent*> watch_events_;
  bool pause_in_read_callback_{false};
  uint64_t max_read_bytes_{std::numeric_limits<uint64_t>::max()};
  unsigned cleanups_{0};
};

TEST_F(IoUringReceiveBackpressureTest, DisableCancelsOnceAndRetainsLateDataUntilResume) {
  Request* read = reads_.back();
  socket_->disableRead();
  ASSERT_EQ(1, cancellations_.size());
  EXPECT_EQ(read, cancellations_.back().first);
  socket_->disableRead();
  EXPECT_EQ(1, cancellations_.size());
  positive(read, 0, "late");
  EXPECT_TRUE(results_.empty());
  EXPECT_EQ("late", socket_->getReadBuffer().toString());
  terminal(read);
  acknowledge(cancellations_.back().second);
  EXPECT_EQ(1, reads_.size());
  EXPECT_TRUE(readv_requests_.empty());
  socket_->enableRead();
  ASSERT_EQ(1, injected_.size());
  EXPECT_EQ(1, reads_.size());
  deliverInjected();
  EXPECT_EQ("late", received_);
  EXPECT_EQ(std::vector<int32_t>({4}), results_);
  EXPECT_EQ(2, reads_.size());

  EXPECT_EQ(std::vector<uint32_t>({0}), buffer_pool_->released_buffers_);
}

TEST_F(IoUringReceiveBackpressureTest, ReadCallbackCanPauseWithoutLosingFollowingCompletions) {
  Request* read = reads_.back();
  pause_in_read_callback_ = true;
  positive(read, 0, "first");
  EXPECT_EQ("first", received_);
  EXPECT_EQ(ReadDisabled, socket_->getStatus());
  ASSERT_EQ(1, cancellations_.size());
  positive(read, 1, "late");
  EXPECT_EQ("late", socket_->getReadBuffer().toString());
  EXPECT_EQ("first", received_);
  terminal(read);
  acknowledge(cancellations_.back().second);
  EXPECT_EQ(1, reads_.size());
  pause_in_read_callback_ = false;
  socket_->enableRead();
  deliverInjected();
  EXPECT_EQ("firstlate", received_);
  EXPECT_EQ(2, reads_.size());
}

TEST_F(IoUringReceiveBackpressureTest, EarlyEnableWaitsForBothCompletionOrders) {
  for (bool cancellation_first : {false, true}) {
    SCOPED_TRACE(cancellation_first);
    const size_t previous_reads = reads_.size();
    Request* read = reads_.back();
    socket_->disableRead();
    ASSERT_FALSE(cancellations_.empty());
    Request* cancel = cancellations_.back().second;
    socket_->enableRead();
    EXPECT_EQ(previous_reads, reads_.size());
    if (cancellation_first) {
      acknowledge(cancel);
      EXPECT_EQ(previous_reads, reads_.size());
      terminal(read);
    } else {
      terminal(read);
      EXPECT_EQ(previous_reads, reads_.size());
      acknowledge(cancel);
    }
    EXPECT_EQ(previous_reads + 1, reads_.size());
    EXPECT_TRUE(readv_requests_.empty());
  }
}

TEST_F(IoUringReceiveBackpressureTest, EarlyEnableRetainsLateTerminalDataWithoutPrematureRearm) {
  Request* read = reads_.back();
  socket_->disableRead();
  ASSERT_EQ(1, cancellations_.size());
  positive(read, 0, "hello");
  socket_->enableRead();
  deliverInjected();
  EXPECT_EQ("hello", received_);
  EXPECT_EQ(1, reads_.size());
  positive(read, 1, "world", false);
  EXPECT_EQ("helloworld", received_);
  EXPECT_EQ(1, reads_.size());
  acknowledge(cancellations_.back().second, -ENOENT);
  EXPECT_EQ(2, reads_.size());
}

TEST_F(IoUringReceiveBackpressureTest, ResourceStopDoesNotStartReadvWhileDisabled) {
  socket_->disableRead();
  ASSERT_EQ(1, cancellations_.size());
  terminal(reads_.back(), -ENOBUFS);
  acknowledge(cancellations_.back().second, -ENOENT);
  EXPECT_EQ(1, reads_.size());
  EXPECT_TRUE(readv_requests_.empty());
  socket_->enableRead();

  ASSERT_EQ(1, readv_requests_.size());
  Request* request = readv_requests_.back();
  auto* readv = static_cast<ReadRequest*>(request);
  memcpy(readv->buf_.get(), "copy", 4);
  terminal(request, 4);
  EXPECT_EQ("copy", received_);
  EXPECT_EQ(2, reads_.size());
}

TEST_F(IoUringReceiveBackpressureTest, RetainedErrorSurvivesInjectedResumeAfterBufferedData) {
  Request* read = reads_.back();
  socket_->disableRead();
  ASSERT_EQ(1, cancellations_.size());
  positive(read, 0, "data");
  terminal(read, -EIO);
  acknowledge(cancellations_.back().second);
  EXPECT_TRUE(results_.empty());
  socket_->enableRead();
  deliverInjected();
  EXPECT_EQ("data", received_);
  EXPECT_EQ(std::vector<int32_t>({4, -EIO}), results_);
  EXPECT_EQ(1, reads_.size());
  EXPECT_EQ(RemoteClosed, socket_->getStatus());
}

TEST_F(IoUringReceiveBackpressureTest, ObservedEofWinsCancelRaceAndIsNotRearmed) {
  socket_->enableCloseEvent(true);
  socket_->disableRead();
  ASSERT_EQ(1, cancellations_.size());
  terminal(reads_.back(), 0);
  EXPECT_EQ(std::vector<uint32_t>({Event::FileReadyType::Closed}), events_);
  EXPECT_EQ(RemoteClosed, socket_->getStatus());
  acknowledge(cancellations_.back().second, -ENOENT);
  socket_->disableRead();
  EXPECT_EQ(RemoteClosed, socket_->getStatus());
  EXPECT_EQ(1, reads_.size());
}

TEST_F(IoUringReceiveBackpressureTest, ObservedEofAllowsBufferedDrainWithoutDuplicateCloseOrRearm) {
  socket_->enableCloseEvent(true);
  Request* read = reads_.back();
  socket_->disableRead();
  ASSERT_EQ(1, cancellations_.size());
  positive(read, 0, "before-fin");
  terminal(read, 0);
  EXPECT_EQ(std::vector<uint32_t>({Event::FileReadyType::Closed}), events_);
  EXPECT_EQ("before-fin", socket_->getReadBuffer().toString());
  EXPECT_EQ(RemoteClosed, socket_->getStatus());
  acknowledge(cancellations_.back().second, -ENOENT);
  socket_->enableRead();
  deliverInjected();
  EXPECT_EQ("before-fin", received_);
  EXPECT_EQ(std::vector<int32_t>({10}), results_);
  EXPECT_EQ((std::vector<uint32_t>{Event::FileReadyType::Closed, Event::FileReadyType::Read}),
            events_);
  EXPECT_EQ(RemoteClosed, socket_->getStatus());
  socket_->enableRead();
  EXPECT_TRUE(injected_.empty());
  EXPECT_EQ(1, reads_.size());
}

TEST_F(IoUringReceiveBackpressureTest, HalfCloseEofWhilePausedDrainsAcrossPartialResumes) {
  Request* read = reads_.back();
  socket_->disableRead();
  ASSERT_EQ(1, cancellations_.size());
  positive(read, 0, "before-fin");
  terminal(read, 0);
  acknowledge(cancellations_.back().second, -ENOENT);
  EXPECT_TRUE(events_.empty());
  EXPECT_EQ(ReadDisabled, socket_->getStatus());
  EXPECT_EQ("before-fin", socket_->getReadBuffer().toString());

  max_read_bytes_ = 4;
  pause_in_read_callback_ = true;
  for (const auto& expected : {"befo", "before-f", "before-fin"}) {
    socket_->enableRead();
    deliverInjected();
    EXPECT_EQ(expected, received_);
    EXPECT_EQ(ReadDisabled, socket_->getStatus());
    EXPECT_TRUE(injected_.empty());
    EXPECT_EQ(1, reads_.size());
  }
  EXPECT_EQ(std::vector<int32_t>({10, 6, 2}), results_);
  EXPECT_EQ(0, socket_->getReadBuffer().length());
  socket_->enableRead();
  deliverInjected();
  EXPECT_EQ(std::vector<int32_t>({10, 6, 2, 0}), results_);
  EXPECT_EQ(RemoteClosed, socket_->getStatus());
  EXPECT_EQ((std::vector<uint32_t>(4, Event::FileReadyType::Read)), events_);
  socket_->enableRead();
  EXPECT_TRUE(injected_.empty());
  EXPECT_EQ(1, reads_.size());
}

TEST_F(IoUringReceiveBackpressureTest, HalfCloseEofWaitsForReadProgressWithoutInjectionSpin) {
  Request* read = reads_.back();
  socket_->disableRead();
  positive(read, 0, "before-fin");
  terminal(read, 0);
  acknowledge(cancellations_.back().second, -ENOENT);
  max_read_bytes_ = 0;
  socket_->enableRead();
  deliverInjected();
  EXPECT_EQ(std::vector<int32_t>({10}), results_);
  EXPECT_TRUE(received_.empty());
  EXPECT_EQ("before-fin", socket_->getReadBuffer().toString());
  EXPECT_TRUE(injected_.empty());
  EXPECT_EQ(ReadEnabled, socket_->getStatus());
  EXPECT_EQ(1, reads_.size());

  // Explicit resume retries the retained payload. Each progressing callback can schedule the
  // remainder, and only the final callback observes EOF; no receive is re-armed after it.
  max_read_bytes_ = 4;
  socket_->enableRead();
  deliverInjected();
  EXPECT_EQ("before-fin", received_);
  EXPECT_EQ(std::vector<int32_t>({10, 10, 6, 2, 0}), results_);
  EXPECT_EQ(RemoteClosed, socket_->getStatus());
  EXPECT_TRUE(injected_.empty());
  EXPECT_EQ(1, reads_.size());
}

TEST_F(IoUringReceiveBackpressureTest, HalfCloseEofWithoutPayloadWaitsForResume) {
  socket_->disableRead();
  terminal(reads_.back(), 0);
  acknowledge(cancellations_.back().second, -ENOENT);
  EXPECT_TRUE(events_.empty());
  EXPECT_EQ(ReadDisabled, socket_->getStatus());
  socket_->enableRead();
  deliverInjected();
  EXPECT_EQ(std::vector<int32_t>({0}), results_);
  EXPECT_EQ(std::vector<uint32_t>({Event::FileReadyType::Read}), events_);
  EXPECT_EQ(RemoteClosed, socket_->getStatus());
  socket_->enableRead();
  EXPECT_TRUE(injected_.empty());
  EXPECT_EQ(1, reads_.size());
}

TEST_F(IoUringReceiveBackpressureTest, FailedCancellationRetriesAfterPositiveProgressOnly) {
  Request* read = reads_.back();
  socket_->disableRead();
  ASSERT_EQ(1, cancellations_.size());
  acknowledge(cancellations_.back().second, -EALREADY);
  EXPECT_EQ(1, cancellations_.size());
  positive(read, 0, "abc");
  ASSERT_EQ(2, cancellations_.size());
  positive(read, 1, "def");
  EXPECT_EQ(2, cancellations_.size());
  terminal(read);
  acknowledge(cancellations_.back().second);
  socket_->enableRead();
  deliverInjected();
  EXPECT_EQ("abcdef", received_);
  EXPECT_EQ(2, reads_.size());
}

TEST_F(IoUringReceiveBackpressureTest,
       CloseWaitsForAckAfterTargetTerminalAndDoesNotDuplicateCancel) {
  Request* read = reads_.back();
  socket_->disableRead();
  ASSERT_EQ(1, cancellations_.size());
  socket_->close(false);
  EXPECT_EQ(1, cancellations_.size());
  terminal(read);
  socket_->close(false);
  socket_->enableRead();
  EXPECT_TRUE(closes_.empty());
  EXPECT_EQ(1, cancellations_.size());
  EXPECT_EQ(1, reads_.size());
  acknowledge(cancellations_.back().second);
  EXPECT_EQ(1, closes_.size());
  socket_->close(false);
  EXPECT_EQ(1, closes_.size());
}

TEST_F(IoUringReceiveBackpressureTest,
       CloseAfterSuccessfulAckWaitsForTargetWithoutDuplicateCancel) {
  Request* read = reads_.back();
  socket_->disableRead();
  ASSERT_EQ(1, cancellations_.size());
  acknowledge(cancellations_.back().second);
  socket_->disableRead();
  socket_->close(false);
  EXPECT_EQ(1, cancellations_.size());
  EXPECT_TRUE(closes_.empty());
  terminal(read);
  EXPECT_EQ(1, closes_.size());
}

TEST_F(IoUringReceiveBackpressureTest, InjectedResumeAfterCloseCannotRearmOrClearCancellation) {
  Request* read = reads_.back();
  socket_->disableRead();
  ASSERT_EQ(1, cancellations_.size());
  positive(read, 0, "late");
  socket_->enableRead();
  ASSERT_EQ(1, injected_.size());
  socket_->close(false);
  deliverInjected();
  EXPECT_TRUE(results_.empty());
  EXPECT_EQ(1, reads_.size());
  EXPECT_EQ(1, cancellations_.size());
  terminal(read);
  EXPECT_TRUE(closes_.empty());
  acknowledge(cancellations_.back().second);
  EXPECT_EQ(1, closes_.size());
}

TEST_F(IoUringReceiveBackpressureTest, KeepFdOpenHandoffWaitsForBothAndCopiesLateBuffers) {
  Request* read = reads_.back();
  socket_->disableRead();
  ASSERT_EQ(1, cancellations_.size());
  positive(read, 0, "handoff");
  std::string handed_off;
  socket_->close(true, [&](Buffer::Instance& buffer) {
    handed_off = buffer.toString();
    buffer.drain(buffer.length());
  });
  terminal(read);
  EXPECT_TRUE(handed_off.empty());
  EXPECT_EQ(0, cleanups_);
  acknowledge(cancellations_.back().second);
  EXPECT_EQ("handoff", handed_off);
  EXPECT_EQ(1, cleanups_);
  EXPECT_EQ(0, worker_->getNumOfSockets());

  EXPECT_EQ(std::vector<uint32_t>({0}), buffer_pool_->released_buffers_);
}

TEST_F(IoUringReceiveBackpressureTest, ShutdownAndWriteCancelAckCannotBypassReadCancel) {
  socket_->disableRead();
  ASSERT_EQ(1, cancellations_.size());
  Request* read_cancel = cancellations_.back().second;
  socket_->shutdown(SHUT_WR);
  ASSERT_EQ(1, shutdowns_.size());
  socket_->close(false);
  socket_->cancelWriteOnWorkerShutdown();
  ASSERT_EQ(2, cancellations_.size());
  Request* write_cancel = cancellations_.back().second;
  ASSERT_TRUE(write_timeout_callback_);
  write_timeout_callback_();
  EXPECT_EQ(2, cancellations_.size());
  terminal(reads_.back());
  socket_->onShutdown(shutdowns_.back(), 0, false);
  destroyRequest(shutdowns_.back());
  acknowledge(write_cancel, -ENOENT);
  EXPECT_TRUE(closes_.empty());
  acknowledge(read_cancel);
  EXPECT_EQ(1, closes_.size());
}

// These checks enter the native watch callback first, then reap its injection through the real
// worker CQ dispatch. The native callback must never call the connection/filter handler itself.
class IoUringPausedCloseWatchTest : public IoUringReceiveBackpressureTest {
public:
  void SetUp() override {
    IoUringReceiveBackpressureTest::SetUp();
    os_injector_ = std::make_unique<TestThreadsafeSingletonInjector<Api::OsSysCallsImpl>>(&os_);
  }
  void stopReceive() {
    socket_->disableRead();
    ASSERT_FALSE(cancellations_.empty());
    terminal(reads_.back());
    EXPECT_TRUE(watch_callbacks_.empty());
    acknowledge(cancellations_.back().second);
    ASSERT_EQ(1, watch_callbacks_.size());
  }
  void deliverInjectedThroughWorker() {
    unsigned callbacks = 0;
    while (!injected_.empty()) {
      ASSERT_LT(callbacks++, 16);
      Request* request = injected_.front();
      injected_.erase(injected_.begin());
      for (auto& owned : owned_requests_) {
        if (owned.get() == request) {
          owned.release(); // The real worker deletes the terminal injected Request.
          break;
        }
      }
      EXPECT_CALL(*mock_, forEveryCompletion(_))
          .WillOnce(
              Invoke([request](const CompletionCb& callback) { callback(request, -EAGAIN, true); }))
          .RetiresOnSaturation();
      worker_->processCompletionsForTest();
    }
  }
  void notify(size_t index = 0) {
    ASSERT_LT(index, watch_callbacks_.size());
    EXPECT_TRUE(watch_callbacks_[index](Event::FileReadyType::Closed).ok());
  }
  testing::StrictMock<Api::MockOsSysCalls> os_;
  std::unique_ptr<TestThreadsafeSingletonInjector<Api::OsSysCallsImpl>> os_injector_;
};

TEST_F(IoUringPausedCloseWatchTest, EofUsesWorkerCqAndDoesNotRearmAfterClose) {
  socket_->enableCloseEvent(true);
  ASSERT_NO_FATAL_FAILURE(stopReceive());
  EXPECT_CALL(os_, recv(0, _, 1, MSG_PEEK | MSG_DONTWAIT))
      .WillOnce(Return(Api::SysCallSizeResult{0, 0}));
  EXPECT_CALL(*watch_events_[0], setEnabled(0));
  notify();
  EXPECT_TRUE(events_.empty());
  ASSERT_EQ(1, injected_.size());
  socket_->disableRead();
  notify(); // Already-disabled persistent `RDHUP` must not peek or inject twice.
  deliverInjectedThroughWorker();
  EXPECT_EQ(std::vector<uint32_t>({Event::FileReadyType::Closed}), events_);
  EXPECT_TRUE(results_.empty());
  EXPECT_EQ(RemoteClosed, socket_->getStatus());
  socket_->enableRead();
  EXPECT_TRUE(injected_.empty());
  EXPECT_EQ(1, reads_.size());
}

TEST_F(IoUringPausedCloseWatchTest, EarlyCloseMayPrecedeBufferedPayloadWhenClosedEnabled) {
  Request* read = reads_.back();
  socket_->enableCloseEvent(true);
  socket_->disableRead();
  positive(read, 0, "retained");
  terminal(read);
  acknowledge(cancellations_.back().second);
  EXPECT_CALL(os_, recv(0, _, 1, MSG_PEEK | MSG_DONTWAIT))
      .WillOnce(Return(Api::SysCallSizeResult{1, 0}));
  notify();
  EXPECT_TRUE(events_.empty());
  deliverInjectedThroughWorker();
  // This is Envoy's early-close contract. Closed-enabled read-disabled connections can close
  // before draining unread bytes; a positive peek is not a proven EOF and cannot become Read(0).
  EXPECT_EQ(std::vector<uint32_t>({Event::FileReadyType::Closed}), events_);
  EXPECT_EQ("retained", socket_->getReadBuffer().toString());
  EXPECT_TRUE(results_.empty());
  socket_->enableRead();
  deliverInjectedThroughWorker();
  EXPECT_EQ("retained", received_);
  EXPECT_EQ(std::vector<int32_t>({8}), results_);
  EXPECT_EQ(1, reads_.size());
}

TEST_F(IoUringPausedCloseWatchTest, HalfClosePositivePeekWaitsForActualPayloadAndEof) {
  ASSERT_NO_FATAL_FAILURE(stopReceive());
  EXPECT_CALL(os_, recv(0, _, 1, MSG_PEEK | MSG_DONTWAIT))
      .WillOnce(Return(Api::SysCallSizeResult{1, 0}));
  notify();
  EXPECT_TRUE(events_.empty());
  EXPECT_TRUE(injected_.empty());
  socket_->enableRead();
  ASSERT_EQ(2, reads_.size());
  positive(reads_.back(), 0, "payload");
  terminal(reads_.back(), 0);
  EXPECT_EQ("payload", received_);
  EXPECT_EQ(std::vector<int32_t>({7, 0}), results_);
  EXPECT_EQ((std::vector<uint32_t>(2, Event::FileReadyType::Read)), events_);
  EXPECT_EQ(RemoteClosed, socket_->getStatus());
  socket_->enableRead();
  EXPECT_EQ(2, reads_.size());
  EXPECT_TRUE(injected_.empty());
}

TEST_F(IoUringPausedCloseWatchTest, PeekResetFollowsRetainedPayloadAndNeverBecomesEof) {
  socket_->disableRead();
  positive(reads_.back(), 0, "data");
  terminal(reads_.back());
  acknowledge(cancellations_.back().second);
  EXPECT_CALL(os_, recv(0, _, 1, MSG_PEEK | MSG_DONTWAIT))
      .WillOnce(Return(Api::SysCallSizeResult{-1, ECONNRESET}));
  notify();
  EXPECT_TRUE(events_.empty());
  max_read_bytes_ = 2;
  socket_->enableRead();
  deliverInjectedThroughWorker();
  EXPECT_EQ("data", received_);
  EXPECT_EQ(std::vector<int32_t>({4, 2, -ECONNRESET}), results_);
  EXPECT_EQ(RemoteClosed, socket_->getStatus());
  socket_->enableRead();
  EXPECT_EQ(1, reads_.size());
  EXPECT_TRUE(injected_.empty());
}

TEST_F(IoUringPausedCloseWatchTest, ResumeBeforeQueuedEventAndRepeatedPauseInvalidateOldTickets) {
  ASSERT_NO_FATAL_FAILURE(stopReceive());
  socket_->enableRead();
  EXPECT_CALL(os_, recv(_, _, _, _)).Times(0);
  notify();
  EXPECT_TRUE(events_.empty());
  testing::Mock::VerifyAndClearExpectations(&os_);
  socket_->disableRead();
  terminal(reads_.back());
  acknowledge(cancellations_.back().second);
  ASSERT_EQ(2, watch_callbacks_.size());
  EXPECT_CALL(os_, recv(_, _, _, _)).Times(0);
  notify(0); // The old ticket remains invalid even though the socket is paused again.
  testing::Mock::VerifyAndClearExpectations(&os_);
  EXPECT_CALL(os_, recv(0, _, 1, MSG_PEEK | MSG_DONTWAIT))
      .WillOnce(Return(Api::SysCallSizeResult{0, 0}));
  notify(1);
  EXPECT_TRUE(events_.empty());
  socket_->enableRead();
  deliverInjectedThroughWorker();
  EXPECT_EQ(std::vector<int32_t>({0}), results_);
  EXPECT_EQ(2, reads_.size());
}

TEST_F(IoUringPausedCloseWatchTest, ResumeBeforeQueuedEarlyCloseReceivesPayloadNormally) {
  socket_->enableCloseEvent(true);
  ASSERT_NO_FATAL_FAILURE(stopReceive());
  EXPECT_CALL(os_, recv(0, _, 1, MSG_PEEK | MSG_DONTWAIT))
      .WillOnce(Return(Api::SysCallSizeResult{1, 0}));
  notify();
  ASSERT_EQ(1, injected_.size());
  socket_->enableRead();
  deliverInjectedThroughWorker();
  EXPECT_TRUE(events_.empty());
  ASSERT_EQ(2, reads_.size());
  positive(reads_.back(), 0, "payload");
  terminal(reads_.back(), 0);
  EXPECT_EQ("payload", received_);
  EXPECT_EQ(std::vector<int32_t>({7}), results_);
  EXPECT_EQ((std::vector<uint32_t>{Event::FileReadyType::Read, Event::FileReadyType::Closed}),
            events_);
}

TEST_F(IoUringPausedCloseWatchTest, CloseBeforeQueuedEventPreventsAccessAfterSocketDestruction) {
  ASSERT_NO_FATAL_FAILURE(stopReceive());
  socket_->close(false);
  ASSERT_EQ(1, closes_.size());
  socket_->onClose(closes_.back(), 0, false);
  destroyRequest(closes_.back());
  dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(0, worker_->getNumOfSockets());
  EXPECT_CALL(os_, recv(_, _, _, _)).Times(0);
  notify(); // Weak ticket expires before the native callback could dereference its raw this.
  EXPECT_TRUE(events_.empty());
  EXPECT_TRUE(injected_.empty());
}

TEST_F(IoUringPausedCloseWatchTest, HandoffBeforeQueuedEventCopiesPayloadAndInvalidatesTicket) {
  socket_->disableRead();
  positive(reads_.back(), 0, "handoff");
  terminal(reads_.back());
  acknowledge(cancellations_.back().second);
  std::string handed_off;
  socket_->close(true, [&handed_off](Buffer::Instance& buffer) { handed_off = buffer.toString(); });
  EXPECT_EQ("handoff", handed_off);
  EXPECT_EQ(0, worker_->getNumOfSockets());
  dispatcher_.clearDeferredDeleteList();
  EXPECT_CALL(os_, recv(_, _, _, _)).Times(0);
  notify();
  EXPECT_TRUE(events_.empty());
  EXPECT_TRUE(injected_.empty());
}

// An unset option preserves the existing disabled-read behavior, including copying fallback.

// A buffered aggregate can be larger than a single kernel completion. Its callback count must
// remain positive when the length exceeds the signed 32-bit range reserved for completion errors.
TEST(IoUringWorkerImplTest, ServerSocketLargeBufferedReadHasPositiveResult) {
  // The sparse mapping is never read or written; suppress trace formatting that copies buffers.
  LogLevelSetter log_level(spdlog::level::warn);
  const uint64_t length = static_cast<uint64_t>(std::numeric_limits<int32_t>::max()) + 1;
  void* memory = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ASSERT_NE(MAP_FAILED, memory);

  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
      .WillOnce(ReturnNew<NiceMock<Event::MockFileEvent>>());
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  IoUringServerSocket* socket_ptr = nullptr;
  uint32_t callback_count = 0;
  auto read_cb = [&socket_ptr, &callback_count, length](uint32_t events) -> absl::Status {
    EXPECT_EQ(Event::FileReadyType::Read, events);
    ++callback_count;
    const ReadParam& param = socket_ptr->getReadParam().ref();
    EXPECT_EQ(std::numeric_limits<int32_t>::max(), param.result_);
    EXPECT_EQ(length, param.buf_.length());
    param.buf_.drain(param.buf_.length());
    return absl::OkStatus();
  };

  Request* read_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadv(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&read_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  IoUringServerSocket socket(0, worker, read_cb, 0, 131072, 16384, false);
  socket_ptr = &socket;
  socket.enableRead();
  socket.disableRead();

  bool released = false;
  Buffer::BufferFragmentImpl fragment(
      memory, length,
      [&released](const void* data, size_t size, const Buffer::BufferFragmentImpl*) {
        EXPECT_EQ(0, munmap(const_cast<void*>(data), size));
        released = true;
      });
  socket.getReadBuffer().addBufferFragment(fragment);

  Request* injected_req = nullptr;
  EXPECT_CALL(mock_io_uring, injectCompletion(0, _, -EAGAIN)).WillOnce(SaveArg<1>(&injected_req));
  socket.enableRead();
  ASSERT_NE(nullptr, injected_req);
  socket.onRead(injected_req, -EAGAIN, true);
  EXPECT_EQ(1, callback_count);
  EXPECT_EQ(0, socket.getReadBuffer().length());
  EXPECT_TRUE(released);

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  delete injected_req;
  delete read_req;
}

} // namespace
} // namespace Io
} // namespace Envoy
