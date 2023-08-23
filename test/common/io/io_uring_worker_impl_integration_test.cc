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
  IoUringSocketTestImpl(os_fd_t fd, IoUringWorkerImpl& parent) : IoUringSocketEntry(fd, parent) {}

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
      : IoUringWorkerImpl(std::move(io_uring_instance), dispatcher) {}

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

  void createServerListenerAndClientSocket() {
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
  createServerListenerAndClientSocket();

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
  createServerListenerAndClientSocket();

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

} // namespace
} // namespace Io
} // namespace Envoy
