#include <atomic>

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/io/io_uring_impl.h"
#include "source/common/io/io_uring_worker_factory_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/io_uring_socket_handle_impl.h"
#include "source/common/thread_local/thread_local_impl.h"

#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace {

class IoUringSocketHandleImplIntegrationTest : public testing::Test {
public:
  IoUringSocketHandleImplIntegrationTest() : should_skip_(!Io::isIoUringSupported()) {}

  void SetUp() override {
    if (should_skip_) {
      GTEST_SKIP();
    }
  }

  void TearDown() override {
    if (!thread_is_shutdown_) {
      instance_.shutdownGlobalThreading();
      instance_.shutdownThread();
    }
  }

  void initialize(bool create_second_thread = false) {
    api_ = Api::createApiForTest(time_system_);
    dispatcher_ = api_->allocateDispatcher("test_thread");
    instance_.registerThread(*dispatcher_, true);

    if (create_second_thread) {
      second_dispatcher_ = api_->allocateDispatcher("test_second_thread");
      instance_.registerThread(*second_dispatcher_, false);
    }

    io_uring_worker_factory_ =
        std::make_unique<Io::IoUringWorkerFactoryImpl>(10, false, 8192, 1000, instance_);
    io_uring_worker_factory_->onWorkerThreadInitialized();

    // Create the thread after the io_uring worker has been initialized, otherwise the dispatcher
    // will quit when there is no any remaining registered event.
    if (create_second_thread) {
      second_thread_ = api_->threadFactory().createThread(
          [this]() -> void { second_dispatcher_->run(Event::Dispatcher::RunType::Block); });
    }
  }

  void createAcceptConnection() {
    // Create an io_uring handle with accept socket.
    fd_ = Api::OsSysCallsSingleton::get().socket(AF_INET, SOCK_STREAM, IPPROTO_TCP).return_value_;
    EXPECT_GE(fd_, 0);
    io_uring_socket_handle_ = std::make_unique<IoUringSocketHandleImpl>(
        *io_uring_worker_factory_, fd_, false, absl::nullopt, false);

    // Listen within the io_uring handle.
    auto local_addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
    io_uring_socket_handle_->bind(local_addr);
    io_uring_socket_handle_->listen(1);

    // Create a socket handle.
    os_fd_t fd = Api::OsSysCallsSingleton::get()
                     .socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP)
                     .return_value_;
    EXPECT_GE(fd, 0);
    io_socket_handle_ = std::make_unique<IoSocketHandleImpl>(fd);
  }

  void createServerConnection() {
    // Create an io_uring handle with server socket.
    fd_ = Api::OsSysCallsSingleton::get().socket(AF_INET, SOCK_STREAM, IPPROTO_TCP).return_value_;
    EXPECT_GE(fd_, 0);
    io_uring_socket_handle_ = std::make_unique<IoUringSocketHandleImpl>(
        *io_uring_worker_factory_, fd_, false, absl::nullopt, true);
  }

  void createClientConnection() {
    // Prepare the listener.
    os_fd_t fd = Api::OsSysCallsSingleton::get()
                     .socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP)
                     .return_value_;
    EXPECT_GE(fd, 0);
    IoHandlePtr listener = std::make_unique<IoSocketHandleImpl>(fd);

    // Listen within the listener.
    auto local_addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
    listener->bind(local_addr);
    listener->listen(1);
    listener->initializeFileEvent(
        *dispatcher_,
        [this, &listener](uint32_t) {
          struct sockaddr addr;
          socklen_t addrlen = sizeof(addr);
          io_socket_handle_ = listener->accept(&addr, &addrlen);
          return absl::OkStatus();
        },
        Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

    // Create an io_uring handle with client socket.
    fd_ = Api::OsSysCallsSingleton::get().socket(AF_INET, SOCK_STREAM, IPPROTO_TCP).return_value_;
    EXPECT_GE(fd_, 0);
    io_uring_socket_handle_ = std::make_unique<IoUringSocketHandleImpl>(
        *io_uring_worker_factory_, fd_, false, absl::nullopt, false);

    int error = -1;
    socklen_t error_size = sizeof(error);
    io_uring_socket_handle_->initializeFileEvent(
        *dispatcher_,
        [this, &error, &error_size](uint32_t events) {
          if (events & Event::FileReadyType::Write) {
            io_uring_socket_handle_->getOption(SOL_SOCKET, SO_ERROR, &error, &error_size);
          }
          return absl::OkStatus();
        },
        Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

    // Connect from io_uring handle.
    io_uring_socket_handle_->connect(listener->localAddress());
    while (error == -1) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
    EXPECT_EQ(error, 0);
  }

  bool should_skip_{false};
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Event::GlobalTimeSystem time_system_;
  ThreadLocal::InstanceImpl instance_;
  std::unique_ptr<Io::IoUringWorkerFactory> io_uring_worker_factory_;
  os_fd_t fd_;
  IoHandlePtr io_uring_socket_handle_;
  IoHandlePtr io_socket_handle_;
  Thread::ThreadPtr second_thread_;
  Event::DispatcherPtr second_dispatcher_;
  bool thread_is_shutdown_{false};
};

TEST_F(IoUringSocketHandleImplIntegrationTest, Close) {
  initialize();
  createServerConnection();

  io_uring_socket_handle_->close();

  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(errno, EBADF);
}

TEST_F(IoUringSocketHandleImplIntegrationTest, CancelAndClose) {
  initialize();
  createServerConnection();

  // Submit the read request.
  io_uring_socket_handle_->initializeFileEvent(
      *dispatcher_, [](uint32_t) { return absl::OkStatus(); }, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Read);
  io_uring_socket_handle_->close();

  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(errno, EBADF);
}

TEST_F(IoUringSocketHandleImplIntegrationTest, Accept) {
  initialize();
  createAcceptConnection();

  bool accepted = false;
  io_uring_socket_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &accepted](uint32_t) {
        struct sockaddr addr;
        socklen_t addrlen = sizeof(addr);
        auto handle = io_uring_socket_handle_->accept(&addr, &addrlen);
        EXPECT_NE(handle, nullptr);
        accepted = true;
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Connect from the socket handle.
  io_socket_handle_->connect(io_uring_socket_handle_->localAddress());
  while (!accepted) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_TRUE(accepted);

  // Close safely.
  io_socket_handle_->close();
  io_uring_socket_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(errno, EBADF);
}

TEST_F(IoUringSocketHandleImplIntegrationTest, AcceptError) {
  initialize();
  createAcceptConnection();

  bool accepted = false;
  io_uring_socket_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &accepted](uint32_t) {
        struct sockaddr addr;
        socklen_t addrlen = sizeof(addr);
        auto handle = io_uring_socket_handle_->accept(&addr, &addrlen);
        EXPECT_EQ(handle, nullptr);
        accepted = true;
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Accept nothing.
  io_uring_socket_handle_->enableFileEvents(Event::FileReadyType::Read);
  io_uring_socket_handle_->activateFileEvents(Event::FileReadyType::Read);
  while (!accepted) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_TRUE(accepted);

  // Close safely.
  io_socket_handle_->close();
  io_uring_socket_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(errno, EBADF);
}

TEST_F(IoUringSocketHandleImplIntegrationTest, Connect) {
  initialize();
  createClientConnection();

  // Close safely.
  io_socket_handle_->close();
  io_uring_socket_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, ConnectError) {
  initialize();

  // Create an io_uring handle with client socket.
  fd_ = Api::OsSysCallsSingleton::get().socket(AF_INET, SOCK_STREAM, IPPROTO_TCP).return_value_;
  EXPECT_GE(fd_, 0);
  io_uring_socket_handle_ = std::make_unique<IoUringSocketHandleImpl>(
      *io_uring_worker_factory_, fd_, false, absl::nullopt, false);

  int original_error = -1;
  socklen_t original_error_size = sizeof(original_error);
  int error = -1;
  socklen_t error_size = sizeof(error);
  io_uring_socket_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &error, &error_size, &original_error, &original_error_size](uint32_t events) {
        if (events & Event::FileReadyType::Write) {
          // We cannot get error with getsockopt since the error has been read within io_uring.
          getsockopt(io_uring_socket_handle_->fdDoNotUse(), SOL_SOCKET, SO_ERROR, &original_error,
                     &original_error_size);
          // The read error will be transferred to the io_uring handle.
          io_uring_socket_handle_->getOption(SOL_SOCKET, SO_ERROR, &error, &error_size);
        }
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Connect from io_uring handle.
  auto local_addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 9999);
  io_uring_socket_handle_->connect(local_addr);
  while (error == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(original_error, 0);
  EXPECT_EQ(error, ECONNREFUSED);

  // Close safely.
  io_uring_socket_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, Read) {
  initialize();
  createClientConnection();

  std::string data = "Hello world";
  Buffer::OwnedImpl write_buffer(data);
  Buffer::OwnedImpl read_buffer;

  io_uring_socket_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &read_buffer, &data](uint32_t event) {
        EXPECT_EQ(event, Event::FileReadyType::Read);
        auto ret = io_uring_socket_handle_->read(read_buffer, absl::nullopt);
        EXPECT_EQ(ret.return_value_, data.size());

        // Read again would expect the EAGAIN returned.
        ret = io_uring_socket_handle_->read(read_buffer, absl::nullopt);
        EXPECT_TRUE(ret.wouldBlock());
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Write from the peer handle.
  io_socket_handle_->write(write_buffer);
  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(read_buffer.toString(), data);

  // Close safely.
  io_socket_handle_->close();
  io_uring_socket_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, ReadContinuity) {
  initialize();
  createClientConnection();

  std::string data = "Hello world";
  Buffer::OwnedImpl write_buffer(data);
  Buffer::OwnedImpl read_buffer;

  bool first_read = true;
  io_uring_socket_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &read_buffer, &first_read](uint32_t event) {
        if (first_read) {
          EXPECT_EQ(event, Event::FileReadyType::Read);
          auto ret = io_uring_socket_handle_->read(read_buffer, 5);
          EXPECT_EQ(ret.return_value_, 5);
        } else {
          EXPECT_EQ(event, Event::FileReadyType::Read);
          auto ret = io_uring_socket_handle_->read(read_buffer, absl::nullopt);
        }
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Write from the peer handle.
  io_socket_handle_->write(write_buffer);
  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(read_buffer.toString(), data.substr(0, 5));

  // Cleanup previous read.
  first_read = false;
  read_buffer.drain(read_buffer.length());
  EXPECT_EQ(write_buffer.length(), 0);

  // Write from the peer handle again to trigger the read event.
  std::string data2 = " again";
  write_buffer.add(data2);
  io_socket_handle_->write(write_buffer);
  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(read_buffer.toString(), data.substr(5) + data2);

  // Close safely.
  io_socket_handle_->close();
  io_uring_socket_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, ReadActively) {
  initialize();
  createClientConnection();

  Buffer::OwnedImpl read_buffer;

  // Read actively.
  auto ret = io_uring_socket_handle_->read(read_buffer, absl::nullopt);
  EXPECT_EQ(ret.wouldBlock(), true);

  // Close safely.
  io_socket_handle_->close();
  io_uring_socket_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, Readv) {
  initialize();
  createClientConnection();

  std::string data = "Hello world";
  Buffer::OwnedImpl write_buffer(data);
  Buffer::OwnedImpl read_buffer;

  io_uring_socket_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &read_buffer, &data](uint32_t event) {
        EXPECT_EQ(event, Event::FileReadyType::Read);
        Buffer::Reservation reservation = read_buffer.reserveForRead();
        auto ret =
            io_uring_socket_handle_->readv(11, reservation.slices(), reservation.numSlices());
        EXPECT_EQ(ret.return_value_, data.size());
        reservation.commit(ret.return_value_);

        // Read again would expect the EAGAIN returned.
        Buffer::Reservation reservation2 = read_buffer.reserveForRead();
        ret = io_uring_socket_handle_->readv(11, reservation2.slices(), reservation2.numSlices());
        EXPECT_TRUE(ret.wouldBlock());
        reservation2.commit(0);
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Write from the peer handle.
  io_socket_handle_->write(write_buffer);
  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(read_buffer.toString(), data);

  // Close safely.
  io_socket_handle_->close();
  io_uring_socket_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, ReadvContinuity) {
  initialize();
  createClientConnection();

  std::string data = "Hello world";
  Buffer::OwnedImpl write_buffer(data);
  Buffer::OwnedImpl read_buffer;

  bool first_read = true;
  io_uring_socket_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &read_buffer, &first_read](uint32_t event) {
        if (first_read) {
          Buffer::Reservation reservation = read_buffer.reserveForRead();
          auto ret =
              io_uring_socket_handle_->readv(5, reservation.slices(), reservation.numSlices());
          EXPECT_EQ(ret.return_value_, 5);
          reservation.commit(ret.return_value_);
        } else {
          EXPECT_EQ(event, Event::FileReadyType::Read);
          Buffer::Reservation reservation = read_buffer.reserveForRead();
          auto ret =
              io_uring_socket_handle_->readv(1024, reservation.slices(), reservation.numSlices());
          reservation.commit(ret.return_value_);
        }
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Write from the peer handle.
  io_socket_handle_->write(write_buffer);
  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(read_buffer.toString(), data.substr(0, 5));

  // Cleanup previous read.
  first_read = false;
  read_buffer.drain(read_buffer.length());
  EXPECT_EQ(write_buffer.length(), 0);

  // Write from the peer handle again to trigger the read event.
  std::string data2 = " again";
  write_buffer.add(data2);
  io_socket_handle_->write(write_buffer);
  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(read_buffer.toString(), data.substr(5) + data2);

  // Close safely.
  io_socket_handle_->close();
  io_uring_socket_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, Write) {
  initialize();
  createClientConnection();

  std::string data = "Hello world";
  Buffer::OwnedImpl write_buffer(data);
  Buffer::OwnedImpl read_buffer;

  io_uring_socket_handle_->initializeFileEvent(
      *dispatcher_, [](uint32_t) { return absl::OkStatus(); }, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Write);

  io_socket_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &read_buffer, &data](uint32_t event) {
        EXPECT_EQ(event, Event::FileReadyType::Read);
        auto ret = io_socket_handle_->read(read_buffer, absl::nullopt);
        EXPECT_EQ(ret.return_value_, data.size());
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Write with the io_uring handle.
  auto ret = io_uring_socket_handle_->write(write_buffer).return_value_;
  EXPECT_EQ(ret, data.size());
  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(read_buffer.toString(), data);

  // Close safely.
  io_socket_handle_->close();
  io_uring_socket_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, Writev) {
  initialize();
  createClientConnection();

  std::string data = "Hello world";
  Buffer::OwnedImpl write_buffer(data);
  Buffer::OwnedImpl read_buffer;

  io_uring_socket_handle_->initializeFileEvent(
      *dispatcher_, [](uint32_t) { return absl::OkStatus(); }, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Write);

  io_socket_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &read_buffer, &data](uint32_t event) {
        EXPECT_EQ(event, Event::FileReadyType::Read);
        auto ret = io_socket_handle_->read(read_buffer, absl::nullopt);
        EXPECT_EQ(ret.return_value_, data.size());
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Write with the io_uring handle.
  auto ret = io_uring_socket_handle_->writev(&write_buffer.getRawSlices()[0], 1).return_value_;
  EXPECT_EQ(ret, data.size());
  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(read_buffer.toString(), data);
  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(read_buffer.toString(), data);

  // Close safely.
  io_socket_handle_->close();
  io_uring_socket_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, Recv) {
  initialize();
  createClientConnection();

  std::string data = "Hello world";
  Buffer::OwnedImpl write_buffer(data);
  Buffer::OwnedImpl peek_buffer;
  Buffer::OwnedImpl recv_buffer;

  io_uring_socket_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &peek_buffer, &recv_buffer, &data](uint32_t event) {
        EXPECT_EQ(event, Event::FileReadyType::Read);
        // Recv with MSG_PEEK will not drain the buffer.
        Buffer::Reservation reservation = peek_buffer.reserveForRead();
        auto ret = io_uring_socket_handle_->recv(reservation.slices()->mem_, 5, MSG_PEEK);
        EXPECT_EQ(ret.return_value_, 5);
        reservation.commit(ret.return_value_);

        // Recv without flags behaves the same as readv.
        Buffer::Reservation reservation2 = recv_buffer.reserveForRead();
        auto ret2 =
            io_uring_socket_handle_->recv(reservation2.slices()->mem_, reservation2.length(), 0);
        EXPECT_EQ(ret2.return_value_, data.size());
        reservation2.commit(ret2.return_value_);
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Write from the peer handle.
  io_socket_handle_->write(write_buffer);
  while (recv_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(peek_buffer.toString(), "Hello");
  EXPECT_EQ(recv_buffer.toString(), data);

  // Close safely.
  io_socket_handle_->close();
  io_uring_socket_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, Bind) {
  initialize();
  createClientConnection();
  // Create an io_uring handle with client socket.
  fd_ = Api::OsSysCallsSingleton::get().socket(AF_INET, SOCK_STREAM, IPPROTO_TCP).return_value_;
  EXPECT_GE(fd_, 0);
  io_uring_socket_handle_ = std::make_unique<IoUringSocketHandleImpl>(
      *io_uring_worker_factory_, fd_, false, absl::nullopt, false);
  auto local_addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  io_uring_socket_handle_->bind(local_addr);

  // Close safely.
  io_uring_socket_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, GetOption) {
  initialize();
  createServerConnection();

  int optval = -1;
  socklen_t optlen = sizeof(optval);
  auto ret = io_uring_socket_handle_->getOption(SOL_SOCKET, SO_REUSEADDR, &optval, &optlen);
  EXPECT_EQ(ret.return_value_, 0);
  EXPECT_EQ(optval, 0);

  // Close safely.
  io_uring_socket_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, Duplicate) {
  initialize();
  createClientConnection();

  IoHandlePtr io_uring_socket_handle_2 = io_uring_socket_handle_->duplicate();

  // Close safely.
  io_uring_socket_handle_2->close();
  io_socket_handle_->close();
  io_uring_socket_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, ActivateReadEvent) {
  initialize();
  createServerConnection();

  // Submit the read request.
  io_uring_socket_handle_->initializeFileEvent(
      *dispatcher_,
      [this](uint32_t event) {
        EXPECT_EQ(event, Event::FileReadyType::Read);
        Buffer::OwnedImpl read_buffer;
        auto ret = io_uring_socket_handle_->read(read_buffer, absl::nullopt);
        EXPECT_TRUE(ret.wouldBlock());
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
  io_uring_socket_handle_->activateFileEvents(Event::FileReadyType::Read);

  // Close safely.
  io_uring_socket_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, ActivateWriteEvent) {
  initialize();
  createClientConnection();

  Buffer::OwnedImpl write_buffer;

  io_uring_socket_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &write_buffer](uint32_t event) {
        EXPECT_EQ(event, Event::FileReadyType::Write);
        auto ret = io_uring_socket_handle_->write(write_buffer);
        EXPECT_TRUE(ret.wouldBlock());
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Write);

  io_uring_socket_handle_->activateFileEvents(Event::FileReadyType::Write);

  // Close safely.
  io_socket_handle_->close();
  io_uring_socket_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, Shutdown) {
  initialize();
  createClientConnection();

  Buffer::OwnedImpl read_buffer;

  io_uring_socket_handle_->initializeFileEvent(
      *dispatcher_, [](uint32_t) { return absl::OkStatus(); }, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Read);

  io_uring_socket_handle_->shutdown(SHUT_WR);
  auto ret = io_socket_handle_->read(read_buffer, absl::nullopt);
  while (ret.wouldBlock()) {
    ret = io_socket_handle_->read(read_buffer, absl::nullopt);
  }
  EXPECT_EQ(ret.return_value_, 0);

  // Close safely.
  io_socket_handle_->close();
  io_uring_socket_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

// Tests the case of a write event will be emitted after remote closed when the read is disabled and
// the write is listened only.
TEST_F(IoUringSocketHandleImplIntegrationTest,
       RemoteCloseWithCloseEventDisabledAndReadEventDisabled) {
  initialize();
  createClientConnection();

  Buffer::OwnedImpl read_buffer;
  bool got_write_event = false;

  io_uring_socket_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &read_buffer, &got_write_event](uint32_t event) {
        EXPECT_EQ(event, Event::FileReadyType::Write);
        auto ret = io_uring_socket_handle_->read(read_buffer, absl::nullopt);
        EXPECT_TRUE(ret.ok());
        EXPECT_EQ(0, ret.return_value_);
        io_uring_socket_handle_->close();
        got_write_event = true;
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
  io_uring_socket_handle_->enableFileEvents(Event::FileReadyType::Write);

  io_socket_handle_->close();
  while (!got_write_event) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(read_buffer.length(), 0);

  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, RemoteCloseWithCloseEventDisabled) {
  initialize();
  createClientConnection();

  std::string data = "Hello world";
  Buffer::OwnedImpl write_buffer(data);
  Buffer::OwnedImpl read_buffer;

  io_uring_socket_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &read_buffer, &data](uint32_t event) {
        EXPECT_EQ(event, Event::FileReadyType::Read);
        auto ret = io_uring_socket_handle_->read(read_buffer, absl::nullopt);
        if (ret.return_value_ > 0) {
          EXPECT_EQ(ret.return_value_, data.size());

          // Read again would expect the EAGAIN returned.
          ret = io_uring_socket_handle_->read(read_buffer, absl::nullopt);
          EXPECT_TRUE(ret.wouldBlock());
        } else if (ret.return_value_ == 0) {
          io_uring_socket_handle_->close();
        }
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
  io_uring_socket_handle_->enableFileEvents(Event::FileReadyType::Read);

  // Write from the peer handle.
  io_socket_handle_->write(write_buffer);
  io_socket_handle_->close();
  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(read_buffer.toString(), data);

  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, RemoteCloseWithCloseEventEnabled) {
  initialize();
  createClientConnection();

  std::string data = "Hello world";
  Buffer::OwnedImpl write_buffer(data);
  Buffer::OwnedImpl read_buffer;

  io_uring_socket_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &read_buffer, &data](uint32_t event) {
        if (event & Event::FileReadyType::Read) {
          auto ret = io_uring_socket_handle_->read(read_buffer, absl::nullopt);
          EXPECT_EQ(ret.return_value_, data.size());

          // Read again would expect the EAGAIN returned.
          ret = io_uring_socket_handle_->read(read_buffer, absl::nullopt);
          EXPECT_TRUE(ret.wouldBlock());
        } else if (event & Event::FileReadyType::Closed) {
          auto ret = io_uring_socket_handle_->read(read_buffer, absl::nullopt);
          EXPECT_EQ(0, ret.return_value_);
          io_uring_socket_handle_->close();
        }
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read | Event::FileReadyType::Closed);

  // Write from the peer handle.
  io_socket_handle_->write(write_buffer);
  io_socket_handle_->close();
  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(read_buffer.toString(), data);

  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

// Ensures IoUringHandleImpl will close the socket on destruction.
TEST_F(IoUringSocketHandleImplIntegrationTest, CloseIoUringSocketOnDestruction) {
  initialize();
  createClientConnection();

  std::string data = "Hello world";
  Buffer::OwnedImpl write_buffer(data);
  Buffer::OwnedImpl read_buffer;

  io_uring_socket_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &read_buffer, &data](uint32_t event) {
        EXPECT_EQ(event, Event::FileReadyType::Read);
        auto ret = io_uring_socket_handle_->read(read_buffer, absl::nullopt);
        EXPECT_EQ(ret.return_value_, data.size());

        // Read again would expect the EAGAIN returned.
        ret = io_uring_socket_handle_->read(read_buffer, absl::nullopt);
        EXPECT_TRUE(ret.wouldBlock());
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Write from the peer handle.
  io_socket_handle_->write(write_buffer);
  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(read_buffer.toString(), data);
  io_uring_socket_handle_.reset();
  while (io_socket_handle_->read(read_buffer, absl::nullopt).return_value_ != 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Close safely.
  io_socket_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

// Ensures IoUringHandleImpl can be released correctly when the IoUringWorker is released earlier.
TEST_F(IoUringSocketHandleImplIntegrationTest, IoUringWorkerEarlyRelease) {
  initialize();
  createClientConnection();

  thread_is_shutdown_ = true;
  instance_.shutdownGlobalThreading();
  instance_.shutdownThread();
}

TEST_F(IoUringSocketHandleImplIntegrationTest, MigrateServerSocketBetweenThreads) {
  initialize(true);
  createClientConnection();

  std::string data = "Hello world";
  Buffer::OwnedImpl write_buffer(data);
  Buffer::OwnedImpl read_buffer;

  io_uring_socket_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &read_buffer](uint32_t event) {
        EXPECT_EQ(event, Event::FileReadyType::Read);
        auto ret = io_uring_socket_handle_->read(read_buffer, 5);
        EXPECT_EQ(ret.return_value_, 5);
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Write from the peer handle.
  io_socket_handle_->write(write_buffer);
  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(read_buffer.toString(), data.substr(0, 5));

  io_uring_socket_handle_->resetFileEvents();
  read_buffer.drain(read_buffer.length());

  // Migrate io_uring between threads.
  std::atomic<bool> initialized_in_new_thread = false;
  std::atomic<bool> read_in_new_thread = false;
  second_dispatcher_->post([this, &second_dispatcher = second_dispatcher_, &read_buffer, &data,
                            &initialized_in_new_thread, &read_in_new_thread]() {
    io_uring_socket_handle_->initializeFileEvent(
        *second_dispatcher,
        [this, &read_buffer, &data, &read_in_new_thread](uint32_t event) {
          EXPECT_EQ(event, Event::FileReadyType::Read);
          auto ret = io_uring_socket_handle_->read(read_buffer, absl::nullopt);
          // For the next read.
          if (read_buffer.length() > data.substr(5).length()) {
            read_in_new_thread = true;
          }
          return absl::OkStatus();
        },
        Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
    initialized_in_new_thread = true;
  });
  while (!initialized_in_new_thread) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Write from the peer handle again to trigger the read event.
  std::string data2 = " again";
  write_buffer.add(data2);
  io_socket_handle_->write(write_buffer);
  while (!read_in_new_thread) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(read_buffer.toString(), data.substr(5) + data2);

  // Close safely.
  io_socket_handle_->close();
  second_dispatcher_->post([this]() { io_uring_socket_handle_->close(); });
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  second_dispatcher_->exit();
  second_thread_->join();
}

} // namespace
} // namespace Network
} // namespace Envoy
