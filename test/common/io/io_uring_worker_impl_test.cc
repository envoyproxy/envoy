#include "source/common/io/io_uring_worker_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/io/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::NiceMock;
using testing::ReturnNew;
using testing::SaveArg;

namespace Envoy {
namespace Io {
namespace {

class IoUringTestSocket : public IoUringSocketEntry {
public:
  IoUringTestSocket(os_fd_t fd, IoUringWorkerImpl& parent) : IoUringSocketEntry(fd, parent) {}

  void close() override {}
  void enable() override {}
  void disable() override {}
  uint64_t write(Buffer::Instance&) override { PANIC("not implement"); }
  uint64_t writev(const Buffer::RawSlice*, uint64_t) override { PANIC("not implement"); }
  void connect(const Network::Address::InstanceConstSharedPtr&) override {}
  void onAccept(int32_t) override {}
  void onClose(int32_t) override {}
  void onCancel(int32_t) override {}
  void onConnect(int32_t) override {}
  void onRead(int32_t) override {}
  void onWrite(int32_t) override {}
};

class IoUringWorkerTestImpl : public IoUringWorkerImpl {
public:
  IoUringWorkerTestImpl(std::unique_ptr<IoUring> io_uring_instance, Event::Dispatcher& dispatcher)
      : IoUringWorkerImpl(std::move(io_uring_instance), dispatcher) {}
  IoUringSocket& addTestSocket(os_fd_t fd, IoUringHandler&) {
    std::unique_ptr<IoUringTestSocket> socket = std::make_unique<IoUringTestSocket>(fd, *this);
    LinkedList::moveIntoListBack(std::move(socket), sockets_);
    return *sockets_.back();
  }

  const std::list<std::unique_ptr<IoUringSocketEntry>>& getSockets() const { return sockets_; }
};

class TestIoUringHandler : public IoUringHandler {
public:
  void onAcceptSocket(AcceptedSocketParam&) override {}
  void onRead(ReadParam&) override {}
  void onWrite(WriteParam&) override {}
};

TEST(IoUringWorkerImplTest, cleanupSocket) {
  Event::MockDispatcher dispatcher;
  std::unique_ptr<IoUring> io_uring_instance = std::make_unique<MockIoUring>();
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

TEST(IoUringWorkerImplTest, delaySubmit) {
  Event::MockDispatcher dispatcher;
  std::unique_ptr<IoUring> io_uring_instance = std::make_unique<MockIoUring>();
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
        struct iovec iov;
        EXPECT_CALL(mock_io_uring, prepareReadv(io_uring_socket.fd(), &iov, 1, 0, _));
        worker.submitReadRequest(io_uring_socket, &iov);
        EXPECT_CALL(mock_io_uring, prepareReadv(io_uring_socket.fd(), &iov, 1, 0, _));
        worker.submitReadRequest(io_uring_socket, &iov);
      }));
  file_event_callback(Event::FileReadyType::Read);

  EXPECT_CALL(mock_io_uring, removeInjectedCompletion(fd));
  EXPECT_CALL(dispatcher, deferredDelete_);
  worker.getSockets().front()->cleanup();
  EXPECT_EQ(0, worker.getSockets().size());
  EXPECT_CALL(dispatcher, clearDeferredDeleteList());
}

} // namespace
} // namespace Io
} // namespace Envoy
