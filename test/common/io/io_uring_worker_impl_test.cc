#include "source/common/io/io_uring_worker_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/io/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::ReturnNew;
using testing::SaveArg;

namespace Envoy {
namespace Io {
namespace {

class IoUringWorkerTestImpl : public IoUringWorkerImpl {
public:
  IoUringWorkerTestImpl(IoUringPtr io_uring_instance, Event::Dispatcher& dispatcher)
      : IoUringWorkerImpl(std::move(io_uring_instance), 5, 8192, dispatcher) {}
  IoUringSocket& addTestSocket(os_fd_t fd, IoUringHandler& io_uring_handler) {
    IoUringSocketEntryPtr socket =
        std::make_unique<IoUringSocketEntry>(fd, *this, io_uring_handler);
    LinkedList::moveIntoListBack(std::move(socket), sockets_);
    return *sockets_.back();
  }

  const std::list<IoUringSocketEntryPtr>& getSockets() const { return sockets_; }
};

class TestIoUringHandler : public IoUringHandler {
public:
  void onAcceptSocket(AcceptedSocketParam&) override {}
  void onRead(ReadParam&) override {}
  void onWrite(WriteParam&) override {}
};

TEST(IoUringWorkerImplTest, CleanupSocket) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());

  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  os_fd_t fd = 11;
  TestIoUringHandler handler;
  SET_SOCKET_INVALID(fd);
  auto& io_uring_socket = worker.addTestSocket(fd, handler);

  EXPECT_EQ(fd, io_uring_socket.fd());
  EXPECT_EQ(1, worker.getSockets().size());
  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  worker.getSockets().front()->cleanup();
  EXPECT_EQ(0, worker.getSockets().size());
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
  TestIoUringHandler handler;
  SET_SOCKET_INVALID(fd);
  auto& io_uring_socket = worker.addTestSocket(fd, handler);

  // The submit only be invoked one time.
  EXPECT_CALL(mock_io_uring, submit());
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&worker, &io_uring_socket, &mock_io_uring](CompletionCb) {
        EXPECT_CALL(mock_io_uring, prepareClose(io_uring_socket.fd(), _));
        auto req = worker.submitCloseRequest(io_uring_socket);
        // Manually delete requests which have to be deleted in request completion callbacks.
        delete req;
        EXPECT_CALL(mock_io_uring, prepareClose(io_uring_socket.fd(), _));
        req = worker.submitCloseRequest(io_uring_socket);
        delete req;
      }));
  file_event_callback(Event::FileReadyType::Read);

  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  worker.getSockets().front()->cleanup();
  EXPECT_EQ(0, worker.getSockets().size());
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
  TestIoUringHandler handler;
  SET_SOCKET_INVALID(fd);

  // The read request added by server socket constructor.
  EXPECT_CALL(mock_io_uring, prepareReadv(fd, _, _, _, _));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  auto& io_uring_socket = worker.addServerSocket(fd, handler);

  // Add a write request.
  std::string data = "Hello";
  Buffer::OwnedImpl buf1;
  buf1.add(data);
  EXPECT_CALL(mock_io_uring, prepareWritev(fd, _, _, _, _)).Times(1).RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  io_uring_socket.write(buf1);

  // Fake an injected completion.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&io_uring_socket](const CompletionCb& cb) {
        auto* req = new BaseRequest(RequestType::Write, io_uring_socket);
        cb(reinterpret_cast<void*>(req), -EAGAIN, true);
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

  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  worker.getSockets().front()->cleanup();
  EXPECT_EQ(0, worker.getSockets().size());
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
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
  TestIoUringHandler handler;
  SET_SOCKET_INVALID(fd);

  // The read request added by server socket constructor.
  EXPECT_CALL(mock_io_uring, prepareReadv(fd, _, _, _, _));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  auto& io_uring_socket = worker.addServerSocket(fd, handler);

  // Fake an injected completion.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&io_uring_socket](const CompletionCb& cb) {
        auto* req = new BaseRequest(RequestType::Write, io_uring_socket);
        cb(reinterpret_cast<void*>(req), -EAGAIN, true);
      }));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  file_event_callback(Event::FileReadyType::Read);

  // When close the socket, expect there still have a incomplete read
  // request, so it has to cancel the request first.
  EXPECT_CALL(mock_io_uring, prepareCancel(_, _));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  io_uring_socket.close();

  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  worker.getSockets().front()->cleanup();
  EXPECT_EQ(0, worker.getSockets().size());
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
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

  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);

  os_fd_t fd = 11;
  TestIoUringHandler handler;
  SET_SOCKET_INVALID(fd);

  // The read request added by server socket constructor.
  EXPECT_CALL(mock_io_uring, prepareReadv(fd, _, _, _, _));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  auto& io_uring_socket = worker.addServerSocket(fd, handler);

  // The IoUringWorker will close all the existing sockets.
  EXPECT_CALL(mock_io_uring, prepareCancel(_, _));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();

  // The IoUringWorker will wait for the socket closed.
  EXPECT_CALL(dispatcher, run(_))
      .WillOnce(Invoke(
          [&io_uring_socket, &file_event_callback, &mock_io_uring, fd](Event::Dispatcher::RunType) {
            // Fake an injected completion.
            EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
                .WillOnce(Invoke([&mock_io_uring, &io_uring_socket, fd](const CompletionCb& cb) {
                  // When the cancel request is done, the close request will be submitted.
                  EXPECT_CALL(mock_io_uring, prepareClose(fd, _));
                  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();

                  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));

                  // Fake the read request cancel completion.
                  auto* read_req = new ReadRequest(io_uring_socket, 10);
                  cb(reinterpret_cast<void*>(read_req), -ECANCELED, false);

                  // Fake the cancel request is done.
                  auto* cancel_req = new BaseRequest(RequestType::Cancel, io_uring_socket);
                  cb(reinterpret_cast<void*>(cancel_req), 0, false);

                  // Fake the close request is done.
                  auto* close_req = new BaseRequest(RequestType::Close, io_uring_socket);
                  cb(reinterpret_cast<void*>(close_req), 0, false);
                }));

            file_event_callback(Event::FileReadyType::Read);
          }));

  EXPECT_CALL(dispatcher, deferredDelete_);
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
}

} // namespace
} // namespace Io
} // namespace Envoy
