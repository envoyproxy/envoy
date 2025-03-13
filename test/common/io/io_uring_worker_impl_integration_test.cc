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

class IoUringSocketTestImpl : public IoUringSocketEntry {
public:
  IoUringSocketTestImpl(os_fd_t fd, IoUringWorkerImpl& parent)
      : IoUringSocketEntry(
            fd, parent, [](uint32_t) { return absl::OkStatus(); }, false) {}

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

  void cleanupForTest() { cleanup(); }

  void write(Buffer::Instance&) override {}
  uint64_t write(const Buffer::RawSlice*, uint64_t) override { return 0; }
  void shutdown(int) override {}

  int32_t accept_result_{-1};
  bool is_accept_injected_completion_{false};
  int32_t connect_result_{-1};
  bool is_connect_injected_completion_{false};
  int32_t read_result_{-1};
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
      : IoUringWorkerImpl(std::move(io_uring_instance), 8192, 1000, dispatcher) {}

  IoUringSocket& addTestSocket(os_fd_t fd) {
    return addSocket(std::make_unique<IoUringSocketTestImpl>(fd, *this));
  }

  const std::list<IoUringSocketEntryPtr>& getSockets() const { return sockets_; }
};

class IoUringWorkerIntegrationTest : public testing::Test {
protected:
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

  void createListenerAndConnectedSocketPair() {
    socket(true, true);
    listen();
    connect();
    accept();
  }

  void createListenerAndSocketPair() {
    // Create the client socket as a blocking socket, so we can observe the client socket's behavior
    // on connecting.
    socket(true, false);
    listen();
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
          return absl::OkStatus();
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

  const bool should_skip_{false};
  os_fd_t listen_socket_{INVALID_SOCKET};
  os_fd_t server_socket_{INVALID_SOCKET};
  os_fd_t client_socket_{INVALID_SOCKET};
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Event::GlobalTimeSystem time_system_;
  std::unique_ptr<IoUringWorkerTestImpl> io_uring_worker_;
};

TEST_F(IoUringWorkerIntegrationTest, Injection) {
  initialize();
  createListenerAndConnectedSocketPair();

  auto& socket =
      dynamic_cast<IoUringSocketTestImpl&>(io_uring_worker_->addTestSocket(server_socket_));
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  socket.injectCompletion(Request::RequestType::Accept);
  socket.injectCompletion(Request::RequestType::Cancel);
  socket.injectCompletion(Request::RequestType::Close);
  socket.injectCompletion(Request::RequestType::Connect);
  socket.injectCompletion(Request::RequestType::Read);
  socket.injectCompletion(Request::RequestType::Write);
  socket.injectCompletion(Request::RequestType::Shutdown);

  // Wait for server socket receive injected completion.
  while (socket.accept_result_ == -1 || socket.cancel_result_ == -1 || socket.close_result_ == -1 ||
         socket.connect_result_ == -1 || socket.read_result_ == -1 || socket.write_result_ == -1 ||
         socket.shutdown_result_ == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_TRUE(socket.is_accept_injected_completion_);
  EXPECT_TRUE(socket.is_cancel_injected_completion_);
  EXPECT_TRUE(socket.is_close_injected_completion_);
  EXPECT_TRUE(socket.is_connect_injected_completion_);
  EXPECT_TRUE(socket.is_read_injected_completion_);
  EXPECT_TRUE(socket.is_write_injected_completion_);
  EXPECT_TRUE(socket.is_shutdown_injected_completion_);
  EXPECT_EQ(socket.accept_result_, -EAGAIN);
  EXPECT_EQ(socket.cancel_result_, -EAGAIN);
  EXPECT_EQ(socket.close_result_, -EAGAIN);
  EXPECT_EQ(socket.connect_result_, -EAGAIN);
  EXPECT_EQ(socket.read_result_, -EAGAIN);
  EXPECT_EQ(socket.write_result_, -EAGAIN);
  EXPECT_EQ(socket.shutdown_result_, -EAGAIN);

  socket.cleanupForTest();
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, MergeInjection) {
  initialize();
  createListenerAndConnectedSocketPair();

  auto& socket =
      dynamic_cast<IoUringSocketTestImpl&>(io_uring_worker_->addTestSocket(server_socket_));
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  socket.injectCompletion(Request::RequestType::Read);
  socket.injectCompletion(Request::RequestType::Read);

  // Waiting for the server socket receive the injected completion.
  while (socket.read_result_ == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_TRUE(socket.is_read_injected_completion_);
  EXPECT_EQ(socket.read_result_, -EAGAIN);
  EXPECT_EQ(socket.nr_completion_, 1);

  socket.cleanupForTest();
}

TEST_F(IoUringWorkerIntegrationTest, ServerSocketRead) {
  initialize();
  createListenerAndConnectedSocketPair();

  absl::optional<int32_t> result = absl::nullopt;
  OptRef<IoUringSocket> socket;
  socket = io_uring_worker_->addServerSocket(
      server_socket_,
      [&socket, &result](uint32_t events) {
        ASSERT(events == Event::FileReadyType::Read);
        EXPECT_NE(absl::nullopt, socket->getReadParam());
        result = socket->getReadParam()->result_;
        return absl::OkStatus();
      },
      false);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Write data through client socket.
  std::string write_data = "hello world";
  Api::OsSysCallsSingleton::get().write(client_socket_, write_data.data(), write_data.size());

  // Waiting for the server socket receive the data.
  while (!result.has_value()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(result.value(), write_data.length());

  socket->close(false);
  runToClose(server_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, ServerSocketReadError) {
  initialize();

  absl::optional<int32_t> result = absl::nullopt;
  OptRef<IoUringSocket> socket;
  socket = io_uring_worker_->addServerSocket(
      -1,
      [&socket, &result](uint32_t events) {
        ASSERT(events == Event::FileReadyType::Read);
        EXPECT_NE(absl::nullopt, socket->getReadParam());
        result = socket->getReadParam()->result_;
        socket->close(false);
        return absl::OkStatus();
      },
      false);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Waiting for the server socket receive the data.
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
  createListenerAndConnectedSocketPair();

  absl::optional<int32_t> result = absl::nullopt;
  OptRef<IoUringSocket> socket;
  socket = io_uring_worker_->addServerSocket(
      server_socket_,
      [&socket, &result](uint32_t events) {
        ASSERT(events == Event::FileReadyType::Read);
        EXPECT_NE(absl::nullopt, socket->getReadParam());
        result = socket->getReadParam()->result_;
        socket->close(false);
        return absl::OkStatus();
      },
      false);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Close the client socket to trigger an error.
  Api::OsSysCallsSingleton::get().close(client_socket_);

  // Waiting for the server socket receive the data.
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
  createListenerAndConnectedSocketPair();

  absl::optional<int32_t> result = absl::nullopt;
  OptRef<IoUringSocket> socket;
  bool drained = false;
  socket = io_uring_worker_->addServerSocket(
      server_socket_,
      [&socket, &result, &drained](uint32_t events) {
        ASSERT(events == Event::FileReadyType::Read);
        EXPECT_NE(absl::nullopt, socket->getReadParam());
        result = socket->getReadParam()->result_;
        if (!drained) {
          socket->getReadParam()->buf_.drain(5);
          drained = true;
        }
        return absl::OkStatus();
      },
      false);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Write data through client socket.
  std::string write_data = "hello world";
  Api::OsSysCallsSingleton::get().write(client_socket_, write_data.data(), write_data.size());

  // Waiting for the server socket receive the data.
  while (!result.has_value()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(result.value(), write_data.length());

  // Emulate enable behavior.
  result.reset();
  socket->disableRead();
  socket->enableRead();
  while (!result.has_value()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(result.value(), write_data.length() - 5);

  socket->close(false);
  runToClose(server_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, ServerSocketWrite) {
  initialize();
  createListenerAndConnectedSocketPair();

  auto& socket = io_uring_worker_->addServerSocket(
      server_socket_, [](uint32_t) { return absl::OkStatus(); }, false);
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

  socket.close(false);
  runToClose(server_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, ServerSocketWriteError) {
  initialize();

  absl::optional<int32_t> result = absl::nullopt;
  OptRef<IoUringSocket> socket;
  socket = io_uring_worker_->addServerSocket(
      -1,
      [&socket, &result](uint32_t events) {
        ASSERT(events == Event::FileReadyType::Write);
        EXPECT_NE(absl::nullopt, socket->getWriteParam());
        result = socket->getWriteParam()->result_;
        socket->close(false);
        return absl::OkStatus();
      },
      false);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);
  socket->disableRead();
  // Waiting for the server socket sending the data.
  std::string write_data = "hello world";
  struct Buffer::RawSlice slice;
  slice.mem_ = write_data.data();
  slice.len_ = write_data.length();
  socket->write(&slice, 1);

  // Waiting for the server socket receive the data.
  while (!result.has_value()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(result.value(), -EBADF);

  runToClose(server_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, ServerSocketWriteTimeout) {
  initialize();
  createListenerAndConnectedSocketPair();

  auto& socket = io_uring_worker_->addServerSocket(
      server_socket_, [](uint32_t) { return absl::OkStatus(); }, false);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Fill peer socket receive buffer.
  std::string write_data(10000000, 'a');
  struct Buffer::RawSlice slice;
  slice.mem_ = write_data.data();
  slice.len_ = write_data.length();
  // The following line may partially complete since write buffer is full.
  socket.write(&slice, 1);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  // The following will block in iouring.
  socket.write(&slice, 1);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Continuously sending the data in server socket until timeout.
  socket.close(false);
  runToClose(server_socket_);

  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, ServerSocketShutdownAfterWrite) {
  initialize();
  createListenerAndConnectedSocketPair();

  auto& socket = io_uring_worker_->addServerSocket(
      server_socket_, [](uint32_t) { return absl::OkStatus(); }, false);
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

  socket.close(false);
  runToClose(server_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, ServerSocketCloseAfterShutdown) {
  initialize();
  createListenerAndConnectedSocketPair();

  auto& socket = io_uring_worker_->addServerSocket(
      server_socket_, [](uint32_t) { return absl::OkStatus(); }, false);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  socket.shutdown(SHUT_WR);

  socket.close(false);
  runToClose(server_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, ServerSocketCloseAfterShutdownWrite) {
  initialize();
  createListenerAndConnectedSocketPair();

  auto& socket = io_uring_worker_->addServerSocket(
      server_socket_, [](uint32_t) { return absl::OkStatus(); }, false);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Waiting for the server socket sending the data.
  std::string write_data = "hello world";
  struct Buffer::RawSlice slice;
  slice.mem_ = write_data.data();
  slice.len_ = write_data.length();
  socket.write(&slice, 1);
  socket.shutdown(SHUT_WR);
  socket.close(false);
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
// In this case, we should deliver this close event if the enable_close_event is true.
TEST_F(IoUringWorkerIntegrationTest, ServerSocketCloseAfterDisabledWithEnableCloseEvent) {
  initialize();
  createListenerAndConnectedSocketPair();

  bool is_closed = false;
  auto& socket = io_uring_worker_->addServerSocket(
      server_socket_,
      [&is_closed](uint32_t events) {
        ASSERT(events == Event::FileReadyType::Closed);
        is_closed = true;
        return absl::OkStatus();
      },
      false);
  socket.enableCloseEvent(true);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);
  // Waiting for the server socket sending the data.
  socket.disableRead();

  Api::OsSysCallsSingleton::get().close(client_socket_);

  while (!is_closed) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  cleanup();
}

// This tests the case when the socket disabled, then a remote close happened.
// Different from the previous cast, in the test the client socket will first write and then
// close.
TEST_F(IoUringWorkerIntegrationTest, ServerSocketReadAndCloseAfterDisabledWithEnableCloseEvent) {
  initialize();
  createListenerAndConnectedSocketPair();

  bool is_closed = false;
  auto& socket = io_uring_worker_->addServerSocket(
      server_socket_,
      [&is_closed](uint32_t events) {
        ASSERT(events == Event::FileReadyType::Closed);
        is_closed = true;
        return absl::OkStatus();
      },
      true);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);
  // Waiting for the server socket sending the data.
  socket.disableRead();

  // Write data through client socket.
  std::string write_data = "hello world";
  Api::OsSysCallsSingleton::get().write(client_socket_, write_data.data(), write_data.size());
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  Api::OsSysCallsSingleton::get().close(client_socket_);

  while (!is_closed) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  cleanup();
}

// This tests the case when the socket disabled, then a remote close happened.
// In this case, we should deliver this remote close by read event if enable_close_event
// is false.
TEST_F(IoUringWorkerIntegrationTest, ServerSocketCloseWithoutEnableCloseEvent) {
  initialize();
  createListenerAndConnectedSocketPair();

  bool is_closed = false;
  OptRef<IoUringSocket> socket;
  socket = io_uring_worker_->addServerSocket(
      server_socket_,
      [&socket, &is_closed](uint32_t events) {
        ASSERT(events == Event::FileReadyType::Read);
        EXPECT_NE(socket->getReadParam(), absl::nullopt);
        EXPECT_EQ(socket->getReadParam()->result_, 0);
        is_closed = true;
        return absl::OkStatus();
      },
      false);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  Api::OsSysCallsSingleton::get().close(client_socket_);

  while (!is_closed) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  cleanup();
}

// This tests the case the socket is disabled, and the close event isn't listened. Then
// a remote close happened, then deliver the remote close by write event.
TEST_F(IoUringWorkerIntegrationTest, ServerSocketCloseAfterDisabledWithoutEnableCloseEvent) {
  initialize();
  createListenerAndConnectedSocketPair();

  bool is_closed = false;
  OptRef<IoUringSocket> socket;
  socket = io_uring_worker_->addServerSocket(
      server_socket_,
      [&socket, &is_closed](uint32_t events) {
        ASSERT(events == Event::FileReadyType::Write);
        EXPECT_NE(socket->getWriteParam(), absl::nullopt);
        EXPECT_EQ(socket->getWriteParam()->result_, 0);
        is_closed = true;
        return absl::OkStatus();
      },
      false);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);
  socket->disableRead();

  Api::OsSysCallsSingleton::get().close(client_socket_);

  while (!is_closed) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, ServerSocketCloseWithAnyRequest) {
  initialize();
  createListenerAndConnectedSocketPair();

  auto& socket = io_uring_worker_->addServerSocket(
      server_socket_, [](uint32_t) { return absl::OkStatus(); }, false);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Disable the socket, then it won't submit any new request.
  socket.disableRead();
  // Write data through client socket, then it will consume the existing request.
  std::string write_data = "hello world";
  Api::OsSysCallsSingleton::get().write(client_socket_, write_data.data(), write_data.size());

  // Running the event loop, to let the iouring worker process the read request.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Close the socket now, it expected the socket will be close directly without cancel.
  socket.close(false);
  runToClose(server_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

// This test ensures that a write after the client reset the connection
// will trigger a closed event.
TEST_F(IoUringWorkerIntegrationTest, ServerSocketWriteWithClientRst) {
  initialize();
  createListenerAndConnectedSocketPair();
  bool expected_read = false;
  bool expected_closed = false;
  auto& socket = io_uring_worker_->addServerSocket(
      server_socket_,
      [&expected_read, &expected_closed](uint32_t events) {
        if (expected_read) {
          EXPECT_TRUE(events | Event::FileReadyType::Read);
          expected_read = false;
          return absl::OkStatus();
        }
        if (expected_closed) {
          EXPECT_TRUE(events | Event::FileReadyType::Closed);
          expected_closed = false;
          return absl::OkStatus();
        }
        // It shouldn't reach here.
        EXPECT_TRUE(false);
        return absl::OkStatus();
      },
      false);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  Api::OsSysCallsSingleton::get().shutdown(client_socket_, SHUT_WR);
  expected_read = true;
  // Running the event loop, to let the iouring worker process the read request.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(expected_read);

  struct linger sl;
  sl.l_onoff = 1;  /* non-zero value enables linger option in kernel */
  sl.l_linger = 0; /* timeout interval in seconds */
  setsockopt(client_socket_, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl));
  Api::OsSysCallsSingleton::get().close(client_socket_);

  expected_closed = true;
  std::string write_data = "hello";
  Buffer::OwnedImpl buffer;
  buffer.add(write_data);
  socket.write(buffer);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(expected_closed);

  // Close the socket now, it expected the socket will be close directly without cancel.
  socket.close(false);
  runToClose(server_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, AddServerSocketWithBuffer) {
  initialize();
  createListenerAndConnectedSocketPair();

  absl::optional<int32_t> result = absl::nullopt;
  OptRef<IoUringSocket> socket;
  std::string data = "hello";
  Buffer::OwnedImpl buffer;
  buffer.add(data);
  socket = io_uring_worker_->addServerSocket(
      server_socket_, buffer,
      [&socket, &result](uint32_t events) {
        ASSERT(events == Event::FileReadyType::Read);
        EXPECT_NE(absl::nullopt, socket->getReadParam());
        result = socket->getReadParam()->result_;
        return absl::OkStatus();
      },
      false);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Waiting for the server socket receive the data.
  while (!result.has_value()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(result.value(), data.length());

  socket->close(false);
  runToClose(server_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, ServerSocketCloseButKeepFdOpen) {
  initialize();
  createListenerAndConnectedSocketPair();

  OptRef<IoUringSocket> socket;
  socket = io_uring_worker_->addServerSocket(
      server_socket_, [](uint32_t) { return absl::OkStatus(); }, false);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  socket->close(true);
  while (!io_uring_worker_->getSockets().empty()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Ensure the server socket is still open.
  std::string write_data = "hello world";
  auto rc =
      Api::OsSysCallsSingleton::get().write(server_socket_, write_data.data(), write_data.size());
  EXPECT_EQ(rc.return_value_, write_data.length());

  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, ServerSocketUpdateFileEventCb) {
  initialize();
  createListenerAndConnectedSocketPair();

  absl::optional<int32_t> result = absl::nullopt;
  OptRef<IoUringSocket> socket;
  socket = io_uring_worker_->addServerSocket(
      server_socket_,
      [](uint32_t) {
        EXPECT_TRUE(false);
        return absl::OkStatus();
      },
      false);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  socket->setFileReadyCb([&socket, &result](uint32_t events) {
    ASSERT(events == Event::FileReadyType::Read);
    EXPECT_NE(absl::nullopt, socket->getReadParam());
    result = socket->getReadParam()->result_;
    return absl::OkStatus();
  });
  // Write data through client socket.
  std::string write_data = "hello world";
  Api::OsSysCallsSingleton::get().write(client_socket_, write_data.data(), write_data.size());

  // Waiting for the server socket receive the data.
  while (!result.has_value()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(result.value(), write_data.length());

  socket->close(false);
  runToClose(server_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, ClientSocketConnect) {
  initialize();
  createListenerAndSocketPair();

  absl::optional<int32_t> result = absl::nullopt;
  OptRef<IoUringSocket> socket;
  socket = io_uring_worker_->addClientSocket(
      client_socket_,
      [&socket, &result](uint32_t events) {
        ASSERT(events == Event::FileReadyType::Write);
        EXPECT_NE(absl::nullopt, socket->getWriteParam());
        result = socket->getWriteParam()->result_;
        return absl::OkStatus();
      },
      false);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Waiting for the client socket connect.
  struct sockaddr_in listen_addr = getListenSocketAddress();
  auto addr = std::make_shared<Network::Address::Ipv4Instance>(&listen_addr);
  socket->connect(addr);

  // Accept through client socket.
  accept();

  // The client socket should be writable.
  while (!result.has_value()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(result.value(), 0);

  socket->close(false);
  runToClose(client_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

TEST_F(IoUringWorkerIntegrationTest, ClientSocketConnectError) {
  initialize();
  createListenerAndSocketPair();

  absl::optional<int32_t> result = absl::nullopt;
  OptRef<IoUringSocket> socket;
  socket = io_uring_worker_->addClientSocket(
      client_socket_,
      [&socket, &result](uint32_t events) {
        ASSERT(events == Event::FileReadyType::Write);
        EXPECT_NE(absl::nullopt, socket->getWriteParam());
        result = socket->getWriteParam()->result_;
        return absl::OkStatus();
      },
      false);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 1);

  // Waiting for the client socket connect.
  auto addr = std::make_shared<Network::Address::Ipv4Instance>(0);
  socket->connect(addr);

  // The client socket should be writable.
  while (!result.has_value()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(result.value(), -ECONNREFUSED);

  socket->close(false);
  runToClose(client_socket_);
  EXPECT_EQ(io_uring_worker_->getSockets().size(), 0);
  cleanup();
}

} // namespace
} // namespace Io
} // namespace Envoy
