#include <sys/socket.h>

#include <queue>

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
  IoUringTestSocket(os_fd_t fd, IoUringWorkerImpl& parent, IoUringHandler& io_uring_handler)
      : IoUringSocketEntry(fd, parent, io_uring_handler) {}

  void onAccept(Request* req, int32_t result, bool injected) override {
    IoUringSocketEntry::onAccept(req, result, injected);
    accept_result_ = result;
    is_accept_injected_completion_ = injected;
    nr_completion_++;
  }
  void onConnect(Request* req, int32_t result, bool injected) override {
    IoUringSocketEntry::onConnect(req, result, injected);
    connect_result_ = result;
    is_connect_injected_completion_ = injected;
    nr_completion_++;
  }
  void onRead(Request* req, int32_t result, bool injected) override {
    IoUringSocketEntry::onRead(req, result, injected);
    read_result_ = result;
    if (!injected && result > 0) {
      auto read_req = static_cast<ReadRequest*>(req);
      read_data_ = std::string(
          reinterpret_cast<char*>(static_cast<ReadRequest*>(read_req)->buf_.get()), result);
    }
    is_read_injected_completion_ = injected;
    nr_completion_++;
  }
  void onWrite(Request* req, int32_t result, bool injected) override {
    IoUringSocketEntry::onWrite(req, result, injected);
    write_result_ = result;
    is_write_injected_completion_ = injected;
    nr_completion_++;
  }
  void onClose(Request* req, int32_t result, bool injected) override {
    IoUringSocketEntry::onClose(req, result, injected);
    close_result_ = result;
    is_close_injected_completion_ = injected;
    nr_completion_++;
  }
  void onCancel(Request* req, int32_t result, bool injected) override {
    IoUringSocketEntry::onCancel(req, result, injected);
    cancel_result_ = result;
    is_cancel_injected_completion_ = injected;
    nr_completion_++;
  }
  void onShutdown(Request* req, int32_t result, bool injected) override {
    IoUringSocketEntry::onShutdown(req, result, injected);
    shutdown_result_ = result;
    is_shutdown_injected_completion_ = injected;
    nr_completion_++;
  }

  int32_t accept_result_{-1};
  bool is_accept_injected_completion_{false};
  int32_t connect_result_{-1};
  bool is_connect_injected_completion_{false};
  int32_t read_result_{-1};
  std::string read_data_{};
  bool is_read_injected_completion_{false};
  int32_t write_result_{-1};
  bool is_write_injected_completion_{false};
  int32_t nr_completion_{0};
  int32_t close_result_{-1};
  bool is_close_injected_completion_{false};
  int32_t cancel_result_{-1};
  bool is_cancel_injected_completion_{false};
  int32_t shutdown_result_{-1};
  bool is_shutdown_injected_completion_{false};
};

class IoUringWorkerTestImpl : public IoUringWorkerImpl {
public:
  IoUringWorkerTestImpl(IoUringPtr io_uring_instance, Event::Dispatcher& dispatcher)
      : IoUringWorkerImpl(std::move(io_uring_instance), 5, 8192, 1000, dispatcher) {}
  IoUringSocket& addTestSocket(os_fd_t fd, IoUringHandler& handler) {
    std::unique_ptr<IoUringTestSocket> socket =
        std::make_unique<IoUringTestSocket>(fd, *this, handler);
    LinkedList::moveIntoListBack(std::move(socket), sockets_);
    return *sockets_.back();
  }

  const std::list<IoUringSocketEntryPtr>& getSockets() const { return sockets_; }
};

class TestIoUringHandler : public IoUringHandler {
public:
  void onAcceptSocket(AcceptedSocketParam& param) override { accept_result_ = param.fd_; }
  void onRead(ReadParam& param) override { on_read_cb_(param); }
  void onWrite(WriteParam& param) override { write_result_ = param.result_; }
  void onClose() override { is_closed = true; }

  void expectRead(std::function<void(ReadParam&)> on_read_cb) { on_read_cb_ = on_read_cb; }

  os_fd_t accept_result_{INVALID_SOCKET};
  int32_t write_result_{0};
  bool is_closed{false};

  std::function<void(ReadParam&)> on_read_cb_;
};

class IoUringWorkerIntegrationTest : public testing::Test {
public:
  IoUringWorkerIntegrationTest() : should_skip_(!isIoUringSupported()) {}

  void SetUp() override {
    if (should_skip_) {
      GTEST_SKIP();
    }
  }

  void initialize() {
    api_ = Api::createApiForTest(time_system_);
    dispatcher_ = api_->allocateDispatcher("test_thread");
    io_uring_worker_ = std::make_unique<IoUringWorkerTestImpl>(
        std::make_unique<IoUringImpl>(20, false), *dispatcher_);
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
          // The server socket should block.
          int flags = fcntl(server_socket_, F_GETFL);
          fcntl(server_socket_, F_SETFL, flags & ~O_NONBLOCK);
        },
        Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
    while (!SOCKET_VALID(server_socket_)) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
    file_event.reset();
  }

  void runToClose(os_fd_t fd) {
    do {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    } while (fcntl(fd, F_GETFD) == 0);
  }

  bool should_skip_{false};
  os_fd_t listen_socket_{INVALID_SOCKET};
  os_fd_t server_socket_{INVALID_SOCKET};
  os_fd_t client_socket_{INVALID_SOCKET};
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Event::GlobalTimeSystem time_system_;
  std::unique_ptr<IoUringWorkerTestImpl> io_uring_worker_;
  TestIoUringHandler io_uring_handler_;
};

TEST_F(IoUringWorkerIntegrationTest, Accept) {
  initialize();
  socket(false, true);
  listen();

  auto& socket = dynamic_cast<IoUringTestSocket&>(
      io_uring_worker_->addTestSocket(listen_socket_, io_uring_handler_));
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Connect through client socket.
  connect();

  // Waiting for the listen socket accept.
  io_uring_worker_->submitAcceptRequest(socket);
  while (socket.accept_result_ == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_GE(socket.accept_result_, 0);
  EXPECT_FALSE(socket.is_accept_injected_completion_);

  socket.cleanup();
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, Connect) {
  initialize();
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

TEST_F(IoUringWorkerIntegrationTest, Read) {
  initialize();
  initializeSockets();

  auto& socket = dynamic_cast<IoUringTestSocket&>(
      io_uring_worker_->addTestSocket(server_socket_, io_uring_handler_));
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Write data through client socket.
  std::string write_data = "hello world";
  Api::OsSysCallsSingleton::get().write(client_socket_, write_data.data(), write_data.size());

  // Waiting for the server socket receive the data.
  io_uring_worker_->submitReadRequest(socket);
  while (socket.read_result_ == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(socket.read_data_, write_data);
  EXPECT_FALSE(socket.is_read_injected_completion_);

  socket.cleanup();
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, Write) {
  initialize();
  initializeSockets();

  auto& socket = dynamic_cast<IoUringTestSocket&>(
      io_uring_worker_->addTestSocket(server_socket_, io_uring_handler_));
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Waiting for the server socket sending the data.
  std::string write_data = "hello world";
  Buffer::OwnedImpl buffer;
  buffer.add(write_data);
  io_uring_worker_->submitWriteRequest(socket, buffer.getRawSlices());
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

TEST_F(IoUringWorkerIntegrationTest, Close) {
  initialize();
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

TEST_F(IoUringWorkerIntegrationTest, CancelRead) {
  initialize();
  initializeSockets();

  auto& socket = dynamic_cast<IoUringTestSocket&>(
      io_uring_worker_->addTestSocket(server_socket_, io_uring_handler_));
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Waiting for the server socket cancel receiving.
  auto req = io_uring_worker_->submitReadRequest(socket);
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

TEST_F(IoUringWorkerIntegrationTest, Injection) {
  initialize();
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

TEST_F(IoUringWorkerIntegrationTest, ReadAndInjection) {
  initialize();
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
  io_uring_worker_->submitReadRequest(socket);
  while (socket.read_result_ == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(socket.read_data_, write_data);
  EXPECT_TRUE(socket.is_write_injected_completion_);
  EXPECT_EQ(socket.write_result_, -EAGAIN);

  socket.cleanup();
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, MergeInjection) {
  initialize();
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

  socket.cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, AcceptSocketAccept) {
  initialize();
  socket(false, true);
  listen();

  auto& socket = io_uring_worker_->addAcceptSocket(listen_socket_, io_uring_handler_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  connect();

  while (io_uring_handler_.accept_result_ == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  socket.close();
  runToClose(listen_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, AcceptSocketDisable) {
  initialize();
  socket(false, true);
  listen();

  auto& socket = io_uring_worker_->addAcceptSocket(listen_socket_, io_uring_handler_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  socket.disable();
  // TODO(zhxie): in fact, we cannot assure that the socket is disabled or enabled after their
  // one shot dispatcher run, but we can assure that the socket has been disabled and enabled in
  // the while-loop run after connect().
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(io_uring_handler_.accept_result_, -1);

  socket.enable();
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  connect();

  while (io_uring_handler_.accept_result_ == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  socket.disable();
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  socket.close();
  runToClose(listen_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, AcceptSocketAcceptOnClosing) {
  initialize();
  socket(false, true);
  listen();

  auto& socket = io_uring_worker_->addAcceptSocket(listen_socket_, io_uring_handler_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Accept when the socket is going to close.
  socket.close();
  connect();

  runToClose(listen_socket_);
  EXPECT_EQ(io_uring_handler_.accept_result_, -1);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, ServerSocketRead) {
  initialize();
  initializeSockets();

  auto& socket = io_uring_worker_->addServerSocket(server_socket_, io_uring_handler_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Write data through client socket.
  std::string write_data = "hello world";
  Api::OsSysCallsSingleton::get().write(client_socket_, write_data.data(), write_data.size());

  // Waiting for the server socket receive the data.
  absl::optional<int32_t> result = absl::nullopt;
  io_uring_handler_.expectRead([&](ReadParam& param) { result = param.result_; });
  while (!result.has_value()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(result.value(), write_data.length());

  socket.close();
  runToClose(server_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, ServerSocketReadError) {
  initialize();

  auto& socket = io_uring_worker_->addServerSocket(-1, io_uring_handler_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Waiting for the server socket receive the data.
  absl::optional<int32_t> result = absl::nullopt;
  io_uring_handler_.expectRead([&](ReadParam& param) {
    result = param.result_;
    socket.close();
  });
  while (!result.has_value()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(result.value(), -EBADF);

  runToClose(server_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, ServerSocketRemoteClose) {
  initialize();
  initializeSockets();

  auto& socket = io_uring_worker_->addServerSocket(server_socket_, io_uring_handler_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Close the client socket to trigger an error.
  Api::OsSysCallsSingleton::get().close(client_socket_);

  // Waiting for the server socket receive the data.
  absl::optional<int32_t> result = absl::nullopt;
  io_uring_handler_.expectRead([&](ReadParam& param) {
    result = param.result_;
    socket.close();
  });
  while (!result.has_value()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(result.value(), 0);

  runToClose(server_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

// Verify that enables a socket will continue reading its remaining data.
TEST_F(IoUringWorkerIntegrationTest, ServerSocketDisable) {
  initialize();
  initializeSockets();

  auto& socket = io_uring_worker_->addServerSocket(server_socket_, io_uring_handler_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Write data through client socket.
  std::string write_data = "hello world";
  Api::OsSysCallsSingleton::get().write(client_socket_, write_data.data(), write_data.size());

  // Waiting for the server socket receive the data.
  absl::optional<int32_t> result = absl::nullopt;
  io_uring_handler_.expectRead([&](ReadParam& param) {
    result = param.result_;
    param.buf_.drain(5);
  });
  while (!result.has_value()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(result.value(), write_data.length());

  // Emulate enable behavior.
  result.reset();
  socket.disable();
  socket.enable();
  io_uring_handler_.expectRead([&](ReadParam& param) { result = param.result_; });
  while (!result.has_value()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(result.value(), write_data.length() - 5);

  socket.close();
  runToClose(server_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, ServerSocketWrite) {
  initialize();
  initializeSockets();

  auto& socket = io_uring_worker_->addServerSocket(server_socket_, io_uring_handler_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Waiting for the server socket sending the data.
  std::string write_data = "hello world";
  struct Buffer::RawSlice slice;
  slice.mem_ = write_data.data();
  slice.len_ = write_data.length();
  socket.write(&slice, 1);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Read data from client socket.
  struct iovec read_iov;
  auto read_buf = std::make_unique<uint8_t[]>(20);
  read_iov.iov_base = read_buf.get();
  read_iov.iov_len = 20;
  auto size = Api::OsSysCallsSingleton::get().readv(client_socket_, &read_iov, 1).return_value_;
  EXPECT_EQ(write_data.size(), size);

  // Test another write interface.
  Buffer::OwnedImpl buffer;
  buffer.add(write_data);
  socket.write(buffer);
  EXPECT_EQ(buffer.length(), 0);

  size = Api::OsSysCallsSingleton::get().readv(client_socket_, &read_iov, 1).return_value_;
  EXPECT_EQ(write_data.size(), size);

  socket.close();
  runToClose(server_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, ServerSocketWriteTimeout) {
  initialize();
  initializeSockets();

  auto& socket = io_uring_worker_->addServerSocket(server_socket_, io_uring_handler_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Fill peer socket receive buffer.
  std::string write_data(10000000, 'a');
  struct Buffer::RawSlice slice;
  slice.mem_ = write_data.data();
  slice.len_ = write_data.length();
  // The following line may partially complete since write buffer is full.
  socket.write(&slice, 1);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  // The following will block in io_uring.
  socket.write(&slice, 1);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Continuously sending the data in server socket until timeout.
  socket.close();
  runToClose(server_socket_);

  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, ServerSocketShutdownAfterWrite) {
  initialize();
  initializeSockets();

  auto& socket = io_uring_worker_->addServerSocket(server_socket_, io_uring_handler_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Waiting for the server socket sending the data.
  std::string write_data = "hello world";
  struct Buffer::RawSlice slice;
  slice.mem_ = write_data.data();
  slice.len_ = write_data.length();
  socket.write(&slice, 1);
  socket.shutdown(SHUT_WR);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Read data from client socket.
  struct iovec read_iov;
  auto read_buf = std::make_unique<uint8_t[]>(20);
  read_iov.iov_base = read_buf.get();
  read_iov.iov_len = 20;
  auto size = Api::OsSysCallsSingleton::get().readv(client_socket_, &read_iov, 1).return_value_;
  EXPECT_EQ(write_data.size(), size);

  socket.close();
  runToClose(server_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, ServerSocketCloseAfterShutdown) {
  initialize();
  initializeSockets();

  auto& socket = io_uring_worker_->addServerSocket(server_socket_, io_uring_handler_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  socket.shutdown(SHUT_WR);

  socket.close();
  runToClose(server_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, ServerSocketCloseAfterShutdownWrite) {
  initialize();
  initializeSockets();

  auto& socket = io_uring_worker_->addServerSocket(server_socket_, io_uring_handler_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Waiting for the server socket sending the data.
  std::string write_data = "hello world";
  struct Buffer::RawSlice slice;
  slice.mem_ = write_data.data();
  slice.len_ = write_data.length();
  socket.write(&slice, 1);
  socket.shutdown(SHUT_WR);
  socket.close();
  runToClose(server_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);

  // Read data from client socket.
  struct iovec read_iov;
  auto read_buf = std::make_unique<uint8_t[]>(20);
  read_iov.iov_base = read_buf.get();
  read_iov.iov_len = 20;
  auto size = Api::OsSysCallsSingleton::get().readv(client_socket_, &read_iov, 1).return_value_;
  EXPECT_EQ(write_data.size(), size);

  size = Api::OsSysCallsSingleton::get().readv(client_socket_, &read_iov, 1).return_value_;
  EXPECT_EQ(0, size);

  cleanup();
}

// This tests the case when the socket disabled, then a remote close happened.
// In this case, we should call IoHandle's onClose, not the onRead to emulate
// a close event.
TEST_F(IoUringWorkerIntegrationTest, ServerSocketCloseAfterDisabled) {
  initialize();
  initializeSockets();

  auto& socket = io_uring_worker_->addServerSocket(server_socket_, io_uring_handler_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);
  // Waiting for the server socket sending the data.
  socket.disable();

  Api::OsSysCallsSingleton::get().close(client_socket_);

  while (!io_uring_handler_.is_closed) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, ServerSocketCloseWithAnyRequest) {
  initialize();
  initializeSockets();

  auto& socket = io_uring_worker_->addServerSocket(server_socket_, io_uring_handler_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Disable the socket, then it won't submit any new request.
  socket.disable();
  // Write data through client socket, then it will consume the existing request.
  std::string write_data = "hello world";
  Api::OsSysCallsSingleton::get().write(client_socket_, write_data.data(), write_data.size());

  // Running the event loop, to let the io_uring worker process the read request.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Close the socket now, it expected the socket will be close directly without cancel.
  socket.close();
  runToClose(server_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

} // namespace
} // namespace Io
} // namespace Envoy
