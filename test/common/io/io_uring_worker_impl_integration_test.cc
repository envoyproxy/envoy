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
  void onRead(int32_t result, bool injected) override {
    IoUringSocketEntry::onRead(result, injected);
    read_result_ = result;
    is_read_injected_completion_ = injected;
    nr_completion_++;
  }
  void onWrite(int32_t result, bool injected) override {
    IoUringSocketEntry::onWrite(result, injected);
    write_result_ = result;
    is_write_injected_completion_ = injected;
    nr_completion_++;
  }

  int32_t read_result_{-1};
  bool is_read_injected_completion_{false};
  int32_t write_result_{-1};
  bool is_write_injected_completion_{false};
  int32_t nr_completion_{0};
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
    io_uring_worker_ = std::make_unique<IoUringWorkerTestImpl>(
        std::make_unique<IoUringImpl>(8, false), *dispatcher_);
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

  std::string serverRead(IoUringTestSocket& socket, uint32_t size) {
    auto buf = std::make_unique<uint8_t[]>(size);
    struct iovec iov;
    iov.iov_base = buf.get();
    iov.iov_len = size;

    // Wait for server socket receiving data.
    socket.read_result_ = -1;
    io_uring_worker_->submitReadRequest(socket, &iov);
    while (socket.read_result_ == -1) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
    EXPECT_EQ(socket.read_result_, size);

    return {static_cast<char*>(static_cast<void*>(buf.release())), size};
  }
  void serverWrite(IoUringTestSocket& socket, std::string& data) {
    size_t length = data.length();

    struct iovec iov;
    iov.iov_base = data.data();
    iov.iov_len = length;

    // Wait for server socket sending data.
    socket.write_result_ = -1;
    io_uring_worker_->submitWritevRequest(socket, &iov, 1);
    while (socket.write_result_ == -1) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
    EXPECT_EQ(socket.write_result_, length);
  }

  Api::SysCallSizeResult clientRead(const struct iovec* data) {
    return Api::OsSysCallsSingleton::get().readv(client_socket_, data, 1);
  }
  void clientWrite(const std::string& data) {
    Api::OsSysCallsSingleton::get().write(client_socket_, data.data(), data.size());
  }

  os_fd_t listen_socket_{INVALID_SOCKET};
  os_fd_t server_socket_{INVALID_SOCKET};
  os_fd_t client_socket_{INVALID_SOCKET};
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Event::GlobalTimeSystem time_system_;
  std::unique_ptr<IoUringWorkerTestImpl> io_uring_worker_;
  TestIoUringHandler io_uring_handler_;
};

TEST_F(IoUringWorkerIntegraionTest, Read) {
  init();
  connect();
  accept();

  auto& socket = dynamic_cast<IoUringTestSocket&>(
      io_uring_worker_->addTestSocket(server_socket_, io_uring_handler_));
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Write data through client socket.
  std::string write_data = "hello world";
  clientWrite(write_data);

  // Read data from server socket.
  std::string read_data = serverRead(socket, write_data.length());

  EXPECT_FALSE(socket.is_read_injected_completion_);
  EXPECT_EQ(read_data, write_data);

  socket.cleanup();
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegraionTest, Write) {
  init();
  connect();
  accept();

  auto& socket = dynamic_cast<IoUringTestSocket&>(
      io_uring_worker_->addTestSocket(server_socket_, io_uring_handler_));
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Write data through server socket.
  std::string write_data = "hello world";
  serverWrite(socket, write_data);

  // Read data from client socket.
  struct iovec read_iov;
  auto read_buf = std::make_unique<uint8_t[]>(20);
  read_iov.iov_base = read_buf.get();
  read_iov.iov_len = 20;
  auto size = clientRead(&read_iov).return_value_;
  EXPECT_EQ(size, write_data.length());

  EXPECT_FALSE(socket.is_write_injected_completion_);
  std::string read_data(static_cast<char*>(read_iov.iov_base), size);
  EXPECT_EQ(read_data, write_data);

  socket.cleanup();
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegraionTest, Injection) {
  init();
  connect();
  accept();

  auto& socket = dynamic_cast<IoUringTestSocket&>(
      io_uring_worker_->addTestSocket(server_socket_, io_uring_handler_));
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  socket.injectCompletion(RequestType::Read);

  // Wait for server socket receive injected completion.
  while (socket.read_result_ == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_TRUE(socket.is_read_injected_completion_);
  EXPECT_EQ(socket.read_result_, -EAGAIN);

  // Write data through client socket.
  socket.read_result_ = -1;
  struct iovec iov;
  auto read_buf = std::make_unique<uint8_t[]>(20);
  iov.iov_base = read_buf.get();
  iov.iov_len = 20;
  io_uring_worker_->submitReadRequest(socket, &iov);
  std::string write_data = "hello world";

  // Expect an inject completion after real read request.
  socket.injectCompletion(RequestType::Write);

  clientWrite(write_data);

  // Waiting for server socket receive data and injected completion.
  while (socket.read_result_ == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  std::string read_data(static_cast<char*>(iov.iov_base), socket.read_result_);
  EXPECT_EQ(read_data, write_data);

  EXPECT_TRUE(socket.is_write_injected_completion_);
  EXPECT_EQ(socket.write_result_, -EAGAIN);

  socket.cleanup();
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegraionTest, MergeInjection) {
  init();
  connect();
  accept();

  auto& socket = dynamic_cast<IoUringTestSocket&>(
      io_uring_worker_->addTestSocket(server_socket_, io_uring_handler_));
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  socket.injectCompletion(RequestType::Read);
  socket.injectCompletion(RequestType::Read);

  // Waiting for the server socket receive the injected completion.
  while (socket.read_result_ == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_TRUE(socket.is_read_injected_completion_);
  EXPECT_EQ(socket.read_result_, -EAGAIN);
  EXPECT_EQ(socket.nr_completion_, 1);
}

} // namespace
} // namespace Io
} // namespace Envoy
