#include <sys/socket.h>

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/io/io_uring_worker_impl.h"
#include "source/common/network/address_impl.h"

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
  void onAccept(int32_t result, bool injected) override {
    IoUringSocketEntry::onAccept(result, injected);
    accept_result_ = result;
    is_accept_injected_completion_ = injected;
    nr_completion_++;
  }
  void onCancel(int32_t result, bool injected) override {
    IoUringSocketEntry::onCancel(result, injected);
    cancel_result_ = result;
    is_cancel_injected_completion_ = injected;
    nr_completion_++;
  }
  void onClose(int32_t result, bool injected) override {
    IoUringSocketEntry::onClose(result, injected);
    close_result_ = result;
    is_close_injected_completion_ = injected;
    nr_completion_++;
  }
  void onConnect(int32_t result, bool injected) override {
    IoUringSocketEntry::onConnect(result, injected);
    connect_result_ = result;
    is_connect_injected_completion_ = injected;
    nr_completion_++;
  }
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

  int32_t accept_result_{-1};
  bool is_accept_injected_completion_{false};
  int32_t cancel_result_{-1};
  bool is_cancel_injected_completion_{false};
  int32_t close_result_{-1};
  bool is_close_injected_completion_{false};
  int32_t connect_result_{-1};
  bool is_connect_injected_completion_{false};
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

  const std::list<IoUringSocketEntryPtr>& getSockets() const { return sockets_; }
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
  }
  void initializeSockets() {
    socket(true, true);
    listen();
    connect();
    accept();
  }
  void cleanup() {
    Api::OsSysCallsSingleton::get().close(client_socket_);
    Api::OsSysCallsSingleton::get().close(server_socket_);
    Api::OsSysCallsSingleton::get().close(listen_socket_);
  }

  struct sockaddr_in getListenSocketAddress() {
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    EXPECT_EQ(getsockname(listen_socket_, reinterpret_cast<struct sockaddr*>(&sin), &len), 0);
    return sin;
  }

  void socket(bool nonblock_listen, bool nonblock_client) {
    listen_socket_ =
        Api::OsSysCallsSingleton::get()
            .socket(AF_INET, SOCK_STREAM | (nonblock_listen ? SOCK_NONBLOCK : 0), IPPROTO_TCP)
            .return_value_;
    EXPECT_TRUE(SOCKET_VALID(listen_socket_));

    client_socket_ =
        Api::OsSysCallsSingleton::get()
            .socket(AF_INET, SOCK_STREAM | (nonblock_client ? SOCK_NONBLOCK : 0), IPPROTO_TCP)
            .return_value_;
    EXPECT_TRUE(SOCKET_VALID(client_socket_));
  }
  void listen() {
    struct sockaddr_in listen_addr;
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = 0;
    EXPECT_EQ(inet_pton(AF_INET, "127.0.0.1", &listen_addr.sin_addr.s_addr), 1);
    EXPECT_EQ(Api::OsSysCallsSingleton::get()
                  .bind(listen_socket_, reinterpret_cast<struct sockaddr*>(&listen_addr),
                        sizeof(listen_addr))
                  .return_value_,
              0);
    EXPECT_EQ(Api::OsSysCallsSingleton::get().listen(listen_socket_, 5).return_value_, 0);
  }
  void connect() {
    struct sockaddr_in listen_addr = getListenSocketAddress();
    Api::OsSysCallsSingleton::get().connect(
        client_socket_, reinterpret_cast<struct sockaddr*>(&listen_addr), sizeof(listen_addr));
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

  os_fd_t listen_socket_{INVALID_SOCKET};
  os_fd_t server_socket_{INVALID_SOCKET};
  os_fd_t client_socket_{INVALID_SOCKET};
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Event::GlobalTimeSystem time_system_;
  std::unique_ptr<IoUringWorkerTestImpl> io_uring_worker_;
  TestIoUringHandler io_uring_handler_;
};

TEST_F(IoUringWorkerIntegraionTest, Accept) {
  init();
  socket(false, true);
  listen();

  auto& socket = dynamic_cast<IoUringTestSocket&>(
      io_uring_worker_->addTestSocket(listen_socket_, io_uring_handler_));
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Connect through client socket.
  connect();

  // Waiting for the listen socket accept.
  struct sockaddr remote_addr;
  socklen_t len = sizeof(remote_addr);
  io_uring_worker_->submitAcceptRequest(socket, reinterpret_cast<sockaddr_storage*>(&remote_addr),
                                        &len);
  while (socket.accept_result_ == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_GE(socket.accept_result_, 0);
  EXPECT_FALSE(socket.is_accept_injected_completion_);

  socket.cleanup();
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegraionTest, Close) {
  init();
  socket(false, true);
  listen();

  auto& socket = dynamic_cast<IoUringTestSocket&>(
      io_uring_worker_->addTestSocket(listen_socket_, io_uring_handler_));
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Waiting for the listen socket close.
  io_uring_worker_->submitCloseRequest(socket);
  while (socket.close_result_ == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(socket.close_result_, 0);
  EXPECT_FALSE(socket.is_close_injected_completion_);

  socket.cleanup();
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegraionTest, Connect) {
  init();
  socket(true, false);
  listen();

  auto& socket = dynamic_cast<IoUringTestSocket&>(
      io_uring_worker_->addTestSocket(client_socket_, io_uring_handler_));
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Waiting for the client socket connect.
  struct sockaddr_in listen_addr = getListenSocketAddress();
  auto addr = std::make_shared<Network::Address::Ipv4Instance>(&listen_addr);
  io_uring_worker_->submitConnectRequest(socket, addr);

  // Accept through client socket.
  accept();

  while (socket.connect_result_ == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(socket.connect_result_, 0);
  EXPECT_FALSE(socket.is_connect_injected_completion_);

  socket.cleanup();
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegraionTest, Read) {
  init();
  initializeSockets();

  auto& socket = dynamic_cast<IoUringTestSocket&>(
      io_uring_worker_->addTestSocket(server_socket_, io_uring_handler_));
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Write data through client socket.
  std::string write_data = "hello world";
  Api::OsSysCallsSingleton::get().write(client_socket_, write_data.data(), write_data.size());

  // Waiting for the server socket receive the data.
  auto read_buf = std::make_unique<uint8_t[]>(20);
  struct iovec iov;
  iov.iov_base = read_buf.get();
  iov.iov_len = 20;
  io_uring_worker_->submitReadRequest(socket, &iov);
  while (socket.read_result_ == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  std::string read_data(static_cast<char*>(iov.iov_base), socket.read_result_);
  EXPECT_EQ(read_data, write_data);
  EXPECT_FALSE(socket.is_read_injected_completion_);

  socket.cleanup();
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegraionTest, Write) {
  init();
  initializeSockets();

  auto& socket = dynamic_cast<IoUringTestSocket&>(
      io_uring_worker_->addTestSocket(server_socket_, io_uring_handler_));
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Waiting for the server socket sending the data.
  std::string write_data = "hello world";
  struct iovec iov;
  iov.iov_base = write_data.data();
  iov.iov_len = write_data.length();
  io_uring_worker_->submitWritevRequest(socket, &iov, 1);
  while (socket.write_result_ == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Read data from client socket.
  struct iovec read_iov;
  auto read_buf = std::make_unique<uint8_t[]>(20);
  read_iov.iov_base = read_buf.get();
  read_iov.iov_len = 20;
  auto size = Api::OsSysCallsSingleton::get().readv(client_socket_, &read_iov, 1).return_value_;
  EXPECT_EQ(size, write_data.length());

  std::string read_data(static_cast<char*>(read_iov.iov_base), size);
  EXPECT_EQ(read_data, write_data);
  EXPECT_FALSE(socket.is_write_injected_completion_);

  socket.cleanup();
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegraionTest, CancelRead) {
  init();
  initializeSockets();

  auto& socket = dynamic_cast<IoUringTestSocket&>(
      io_uring_worker_->addTestSocket(server_socket_, io_uring_handler_));
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Waiting for the server socket cancel receiving.
  auto read_buf = std::make_unique<uint8_t[]>(20);
  struct iovec iov;
  iov.iov_base = read_buf.get();
  iov.iov_len = 20;
  auto req = io_uring_worker_->submitReadRequest(socket, &iov);
  io_uring_worker_->submitCancelRequest(socket, req);
  while (socket.cancel_result_ == -1 || socket.read_result_ == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(socket.cancel_result_, 0);
  EXPECT_EQ(socket.read_result_, -ECANCELED);
  EXPECT_FALSE(socket.is_cancel_injected_completion_);
  EXPECT_FALSE(socket.is_read_injected_completion_);

  socket.cleanup();
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegraionTest, Injection) {
  init();
  initializeSockets();

  auto& socket = dynamic_cast<IoUringTestSocket&>(
      io_uring_worker_->addTestSocket(server_socket_, io_uring_handler_));
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  socket.injectCompletion(RequestType::Accept);
  socket.injectCompletion(RequestType::Cancel);
  socket.injectCompletion(RequestType::Close);
  socket.injectCompletion(RequestType::Connect);
  socket.injectCompletion(RequestType::Read);
  socket.injectCompletion(RequestType::Write);

  // Wait for server socket receive injected completion.
  while (socket.accept_result_ == -1 || socket.cancel_result_ == -1 || socket.close_result_ == -1 ||
         socket.connect_result_ == -1 || socket.read_result_ == -1 || socket.write_result_ == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_TRUE(socket.is_accept_injected_completion_);
  EXPECT_TRUE(socket.is_cancel_injected_completion_);
  EXPECT_TRUE(socket.is_close_injected_completion_);
  EXPECT_TRUE(socket.is_connect_injected_completion_);
  EXPECT_TRUE(socket.is_read_injected_completion_);
  EXPECT_TRUE(socket.is_write_injected_completion_);
  EXPECT_EQ(socket.accept_result_, -EAGAIN);
  EXPECT_EQ(socket.cancel_result_, -EAGAIN);
  EXPECT_EQ(socket.close_result_, -EAGAIN);
  EXPECT_EQ(socket.connect_result_, -EAGAIN);
  EXPECT_EQ(socket.read_result_, -EAGAIN);
  EXPECT_EQ(socket.write_result_, -EAGAIN);

  socket.cleanup();
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegraionTest, ReadAndInjection) {
  init();
  initializeSockets();

  auto& socket = dynamic_cast<IoUringTestSocket&>(
      io_uring_worker_->addTestSocket(server_socket_, io_uring_handler_));
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Expect an inject completion after real read request.
  socket.injectCompletion(RequestType::Write);

  // Write data through client socket.
  std::string write_data = "hello world";
  Api::OsSysCallsSingleton::get().write(client_socket_, write_data.data(), write_data.size());

  // Waiting for server socket receive data and injected completion.
  struct iovec iov;
  auto read_buf = std::make_unique<uint8_t[]>(20);
  iov.iov_base = read_buf.get();
  iov.iov_len = 20;
  io_uring_worker_->submitReadRequest(socket, &iov);
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
  initializeSockets();

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
