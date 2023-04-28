#include "source/common/io/io_uring_worker_impl.h"

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

class IoUringWorkerTestImpl : public IoUringWorkerImpl {
public:
  IoUringWorkerTestImpl(IoUringPtr io_uring_instance, Event::Dispatcher& dispatcher)
      : IoUringWorkerImpl(std::move(io_uring_instance), 5, 8192, 1000, dispatcher) {}
  IoUringSocket& addTestSocket(os_fd_t fd, IoUringHandler& io_uring_handler) {
    IoUringSocketEntryPtr socket =
        std::make_unique<IoUringSocketEntry>(fd, *this, io_uring_handler, false);
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
  void onRemoteClose() override {}
  void onLocalClose() override {}
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

  void* read_req = nullptr;
  // The read request added by server socket constructor.
  EXPECT_CALL(mock_io_uring, prepareReadv(fd, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&read_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  auto& io_uring_socket = worker.addServerSocket(fd, handler, false);

  // Add a write request.
  std::string data = "Hello";
  Buffer::OwnedImpl buf1;
  buf1.add(data);
  void* write_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareWritev(fd, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&write_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
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

  // Start the close process.
  void* cancel_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareCancel(_, _))
      .WillOnce(DoAll(SaveArg<1>(&cancel_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  EXPECT_CALL(dispatcher, createTimer_(_)).WillOnce(ReturnNew<NiceMock<Event::MockTimer>>());
  io_uring_socket.close(false);

  // Finish the read, cancel and write request, then expect the close request submitted.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&read_req, &cancel_req, &write_req](const CompletionCb& cb) {
        cb(reinterpret_cast<void*>(read_req), -EAGAIN, false);
        cb(reinterpret_cast<void*>(cancel_req), 0, false);
        cb(reinterpret_cast<void*>(write_req), -EAGAIN, false);
      }));
  void* close_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareClose(_, _))
      .WillOnce(DoAll(SaveArg<1>(&close_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  file_event_callback(Event::FileReadyType::Read);

  // After the close request finished, the socket will be cleanup.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&close_req](const CompletionCb& cb) {
        cb(reinterpret_cast<void*>(close_req), 0, false);
      }));
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
  TestIoUringHandler handler;
  SET_SOCKET_INVALID(fd);

  // The read request added by server socket constructor.
  void* read_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadv(fd, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&read_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  auto& io_uring_socket = worker.addServerSocket(fd, handler, false);

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
  void* cancel_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareCancel(_, _))
      .WillOnce(DoAll(SaveArg<1>(&cancel_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  io_uring_socket.close(false);

  // Finish the read and cancel request, then expect the close request submitted.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&read_req, &cancel_req](const CompletionCb& cb) {
        cb(reinterpret_cast<void*>(read_req), -EAGAIN, false);
        cb(reinterpret_cast<void*>(cancel_req), 0, false);
      }));
  void* close_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareClose(_, _))
      .WillOnce(DoAll(SaveArg<1>(&close_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  file_event_callback(Event::FileReadyType::Read);

  // After the close request finished, the socket will be cleanup.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&close_req](const CompletionCb& cb) {
        cb(reinterpret_cast<void*>(close_req), 0, false);
      }));
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
  TestIoUringHandler handler;
  SET_SOCKET_INVALID(fd);

  // The read request added by server socket constructor.
  void* read_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadv(fd, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&read_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  worker->addServerSocket(fd, handler, false);

  // The IoUringWorker will close all the existing sockets.
  void* cancel_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareCancel(_, _))
      .WillOnce(DoAll(SaveArg<1>(&cancel_req), Return<IoUringResult>(IoUringResult::Ok)))
      .RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();

  // The IoUringWorker will wait for the socket closed.
  EXPECT_CALL(dispatcher, run(_))
      .WillOnce(Invoke([&file_event_callback, &mock_io_uring, fd, &read_req,
                        &cancel_req](Event::Dispatcher::RunType) {
        // Fake an injected completion.
        EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
            .WillOnce(Invoke([&mock_io_uring, fd, &read_req, &cancel_req](const CompletionCb& cb) {
              // When the cancel request is done, the close request will be submitted.
              void* close_req = nullptr;
              EXPECT_CALL(mock_io_uring, prepareClose(fd, _))
                  .WillOnce(DoAll(SaveArg<1>(&close_req), Return<IoUringResult>(IoUringResult::Ok)))
                  .RetiresOnSaturation();
              EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();

              EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));

              // Fake the read request cancel completion.
              cb(reinterpret_cast<void*>(read_req), -ECANCELED, false);

              // Fake the cancel request is done.
              cb(reinterpret_cast<void*>(cancel_req), 0, false);

              // Fake the close request is done.
              cb(reinterpret_cast<void*>(close_req), 0, false);
            }));

        file_event_callback(Event::FileReadyType::Read);
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
  TestIoUringHandler handler;
  SET_SOCKET_INVALID(fd);

  // The read request added by server socket constructor.
  void* read_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadv(fd, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&read_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  auto& io_uring_socket = worker.addServerSocket(fd, handler, false);

  // Disable the socket, then there will be no new read request.
  io_uring_socket.disable();
  // Fake the read request finish.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&read_req](const CompletionCb& cb) {
        cb(reinterpret_cast<void*>(read_req), -EAGAIN, false);
      }));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  file_event_callback(Event::FileReadyType::Read);

  void* write_req = nullptr;
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

  void* close_req = nullptr;
  // Finish the read and cancel request, then expect the close request submitted.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&mock_io_uring, &write_req, &close_req](const CompletionCb& cb) {
        EXPECT_CALL(mock_io_uring, prepareClose(_, _))
            .WillOnce(DoAll(SaveArg<1>(&close_req), Return<IoUringResult>(IoUringResult::Ok)))
            .RetiresOnSaturation();
        EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();

        cb(reinterpret_cast<void*>(write_req), -EAGAIN, false);
      }));
  file_event_callback(Event::FileReadyType::Read);

  // After the close request finished, the socket will be cleanup.
  EXPECT_CALL(mock_io_uring, forEveryCompletion(_))
      .WillOnce(Invoke([&close_req](const CompletionCb& cb) {
        cb(reinterpret_cast<void*>(close_req), 0, false);
      }));
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
  MockIoUringHandler handler;

  void* read_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadv(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&read_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  IoUringServerSocket socket(0, worker, handler, 0, true);
  socket.disable();

  // Consumes the first read request.
  void* read_req2 = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadv(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&read_req2), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  socket.onRead(static_cast<Request*>(read_req), 1, false);
  // Trigger a further remote close.
  EXPECT_CALL(handler, onRemoteClose());
  socket.onRead(nullptr, 0, false);

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  delete static_cast<Request*>(read_req);
  delete static_cast<Request*>(read_req2);
}

TEST(IoUringWorkerImplTest, NoOnWriteCallingBackInShutdownWriteSocketInjection) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);
  MockIoUringHandler handler;
  void* read_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareReadv(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&read_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  IoUringServerSocket socket(0, worker, handler, 0, false);

  // Shutdown and then shutdown completes.
  EXPECT_CALL(mock_io_uring, submit());
  void* shutdown_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareShutdown(socket.fd(), _, _))
      .WillOnce(DoAll(SaveArg<2>(&shutdown_req), Return<IoUringResult>(IoUringResult::Ok)));
  socket.shutdown(SHUT_WR);
  socket.onShutdown(static_cast<Request*>(shutdown_req), 0, false);

  // onWrite happens after the shutdown completed will not trigger calling back.
  socket.onWrite(nullptr, 0, true);

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  delete static_cast<Request*>(read_req);
  delete static_cast<Request*>(shutdown_req);
}

TEST(IoUringWorkerImplTest, AcceptSocketAvoidDuplicateClose) {
  Event::MockDispatcher dispatcher;
  IoUringPtr io_uring_instance = std::make_unique<MockIoUring>();
  MockIoUring& mock_io_uring = *dynamic_cast<MockIoUring*>(io_uring_instance.get());
  EXPECT_CALL(mock_io_uring, registerEventfd());
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                           Event::FileReadyType::Read));
  IoUringWorkerTestImpl worker(std::move(io_uring_instance), dispatcher);
  MockIoUringHandler handler;

  void* accept_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareAccept(_, _, _, _))
      .WillOnce(DoAll(SaveArg<3>(&accept_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  IoUringAcceptSocket socket(0, worker, handler, 1, true);

  // Close the socket.
  void* cancel_req = nullptr;
  EXPECT_CALL(handler, onLocalClose()).Times(2).RetiresOnSaturation();
  EXPECT_CALL(mock_io_uring, prepareCancel(_, _))
      .WillOnce(DoAll(SaveArg<1>(&cancel_req), Return<IoUringResult>(IoUringResult::Ok)));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  socket.close(false);

  // Another close happens in accept callback.
  void* close_req = nullptr;
  EXPECT_CALL(mock_io_uring, prepareClose(_, _)).WillOnce(Invoke([&](os_fd_t, void* user_data) {
    socket.close(false);
    close_req = user_data;
    return IoUringResult::Ok;
  }));
  EXPECT_CALL(mock_io_uring, submit()).Times(1).RetiresOnSaturation();
  socket.onAccept(static_cast<Request*>(accept_req), -ECANCELED, false);

  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
  delete static_cast<Request*>(accept_req);
  delete static_cast<Request*>(cancel_req);
  delete static_cast<Request*>(close_req);
}

} // namespace
} // namespace Io
} // namespace Envoy
