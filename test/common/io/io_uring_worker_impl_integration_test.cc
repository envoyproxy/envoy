#include <sys/socket.h>

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/io/io_uring_worker_impl.h"

#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

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
  void onRead(int32_t result) override { read_result_ = result; }
  void onWrite(int32_t result) override { write_result_ = result; }

  int32_t read_result_{-1};
  int32_t write_result_{-1};
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

class IoUringWorkerIntegraionTest : public testing::Test {
public:
  void init() {
    if (!isIoUringSupported()) {
      GTEST_SKIP();
    }
    api_ = Api::createApiForTest(time_system_);
    dispatcher_ = api_->allocateDispatcher("test_thread");
    listen_socket_ = Api::OsSysCallsSingleton::get()
                         .socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP)
                         .return_value_;
    EXPECT_TRUE(SOCKET_VALID(listen_socket_));
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = 0;
    EXPECT_EQ(inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr.s_addr), 1);
    EXPECT_EQ(Api::OsSysCallsSingleton::get()
                  .bind(listen_socket_, reinterpret_cast<struct sockaddr*>(&server_addr),
                        sizeof(server_addr))
                  .return_value_,
              0);
    EXPECT_EQ(Api::OsSysCallsSingleton::get().listen(listen_socket_, 5).return_value_, 0);

    // Using non blocking for the client socket, then we won't block on the test thread.
    client_socket_ = Api::OsSysCallsSingleton::get()
                         .socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP)
                         .return_value_;
    EXPECT_TRUE(SOCKET_VALID(client_socket_));
  }

  void cleanup() {
    Api::OsSysCallsSingleton::get().close(client_socket_);
    Api::OsSysCallsSingleton::get().close(server_socket_);
    Api::OsSysCallsSingleton::get().close(listen_socket_);
  }

  uint32_t getServerPort() {
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    EXPECT_EQ(getsockname(listen_socket_, reinterpret_cast<struct sockaddr*>(&sin), &len), 0);
    return sin.sin_port;
  }

  void connect() {
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = getServerPort();
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    Api::OsSysCallsSingleton::get().connect(
        client_socket_, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr));
    EXPECT_EQ(errno, EINPROGRESS);
  }

  void accept() {
    auto file_event = dispatcher_->createFileEvent(
        listen_socket_,
        [this](uint32_t events) {
          EXPECT_EQ(events, Event::FileReadyType::Read);
          struct sockaddr_in server_addr;
          socklen_t server_addr_len = sizeof(server_addr);
          server_socket_ =
              Api::OsSysCallsSingleton::get()
                  .accept(listen_socket_, reinterpret_cast<struct sockaddr*>(&server_addr),
                          &server_addr_len)
                  .return_value_;
          EXPECT_TRUE(SOCKET_VALID(server_socket_));
        },
        Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
    while (!SOCKET_VALID(server_socket_)) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
    file_event.reset();
  }

  void write(const std::string& data) {
    Api::OsSysCallsSingleton::get().write(client_socket_, data.data(), data.size());
  }

  os_fd_t listen_socket_{INVALID_SOCKET};
  os_fd_t server_socket_{INVALID_SOCKET};
  os_fd_t client_socket_{INVALID_SOCKET};
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Event::GlobalTimeSystem time_system_;
};

TEST_F(IoUringWorkerIntegraionTest, Basic) {
  init();
  connect();
  accept();

  IoUringWorkerTestImpl io_uring_worker(std::make_unique<IoUringImpl>(8, false), *dispatcher_);
  auto handler = TestIoUringHandler();
  auto& io_uring_socket =
      dynamic_cast<IoUringTestSocket&>(io_uring_worker.addTestSocket(server_socket_, handler));
  EXPECT_EQ(io_uring_worker.getSockets().size(), 1);

  // Write data through client socket.
  struct iovec iov;
  auto read_buf = std::make_unique<uint8_t[]>(20);
  iov.iov_base = read_buf.get();
  iov.iov_len = 20;
  io_uring_worker.submitReadRequest(io_uring_socket, &iov);
  std::string write_data = "hello world";
  write(write_data);

  // Waiting for the server socket receive the data.
  while (io_uring_socket.read_result_ == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  std::string read_data(static_cast<char*>(iov.iov_base), io_uring_socket.read_result_);
  EXPECT_EQ(read_data, write_data);

  io_uring_socket.cleanup();
  EXPECT_EQ(io_uring_worker.getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegraionTest, Injection) {
  init();
  connect();
  accept();

  IoUringWorkerTestImpl io_uring_worker(std::make_unique<IoUringImpl>(8, false), *dispatcher_);
  auto handler = TestIoUringHandler();
  auto& io_uring_socket =
      dynamic_cast<IoUringTestSocket&>(io_uring_worker.addTestSocket(server_socket_, handler));
  EXPECT_EQ(io_uring_worker.getSockets().size(), 1);

  io_uring_socket.injectCompletion(RequestType::Read);

  // Waiting for the server socket receive the injected completion.
  while (io_uring_socket.read_result_ == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(io_uring_socket.read_result_, -EAGAIN);

  // Write data through client socket.
  io_uring_socket.read_result_ = -1;
  struct iovec iov;
  auto read_buf = std::make_unique<uint8_t[]>(20);
  iov.iov_base = read_buf.get();
  iov.iov_len = 20;
  io_uring_worker.submitReadRequest(io_uring_socket, &iov);
  std::string write_data = "hello world";

  // Expect an inject completion after real read request.
  io_uring_socket.injectCompletion(RequestType::Write);

  write(write_data);

  // Waiting for the server socket receive the data and injected completion.
  while (io_uring_socket.read_result_ == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  std::string read_data(static_cast<char*>(iov.iov_base), io_uring_socket.read_result_);
  EXPECT_EQ(read_data, write_data);

  EXPECT_EQ(io_uring_socket.write_result_, -EAGAIN);

  io_uring_socket.cleanup();
  EXPECT_EQ(io_uring_worker.getSockets().size(), 0);
  cleanup();
}

} // namespace
} // namespace Io
} // namespace Envoy
