#include <atomic>

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/io/io_uring_factory_impl.h"
#include "source/common/io/io_uring_impl.h"
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

  void threadRoutine() { second_dispatcher_->run(Event::Dispatcher::RunType::Block); }

  void initialize(bool create_second_thread = false) {
    api_ = Api::createApiForTest(time_system_);
    dispatcher_ = api_->allocateDispatcher("test_thread");
    instance_.registerThread(*dispatcher_, true);

    if (create_second_thread) {
      second_dispatcher_ = api_->allocateDispatcher("test_second_thread");
      instance_.registerThread(*second_dispatcher_, false);
    }

    io_uring_factory_ =
        std::make_unique<Io::IoUringFactoryImpl>(10, false, 5, 8192, 1000, instance_);
    io_uring_factory_->onWorkerThreadInitialized();

    // create the thread after the io-uring worker initialized, otherwise the
    // dispatcher will exit when there is no any registered event.
    if (create_second_thread) {
      second_thread_ = api_->threadFactory().createThread([this]() -> void { threadRoutine(); });
    }

    fd_ = Api::OsSysCallsSingleton::get().socket(AF_INET, SOCK_STREAM, IPPROTO_TCP).return_value_;
    EXPECT_GE(fd_, 0);
    io_handle_ = std::make_unique<IoUringSocketHandleImpl>(*io_uring_factory_, fd_);

    os_fd_t fd = Api::OsSysCallsSingleton::get()
                     .socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP)
                     .return_value_;
    EXPECT_GE(fd, 0);
    peer_io_handle_ = std::make_unique<IoSocketHandleImpl>(fd);
  }

  bool should_skip_{false};
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Event::GlobalTimeSystem time_system_;
  ThreadLocal::InstanceImpl instance_;
  std::unique_ptr<Io::IoUringFactory> io_uring_factory_;
  os_fd_t fd_;
  IoHandlePtr io_handle_;
  IoHandlePtr peer_io_handle_;
  Thread::ThreadPtr second_thread_;
  Event::DispatcherPtr second_dispatcher_;
  bool thread_is_shutdown_{false};
};

TEST_F(IoUringSocketHandleImplIntegrationTest, Close) {
  initialize();

  io_handle_->close();

  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(errno, EBADF);
}

TEST_F(IoUringSocketHandleImplIntegrationTest, CancelAndClose) {
  initialize();

  // Submit accept requests with listening.
  io_handle_->listen(5);
  io_handle_->initializeFileEvent(
      *dispatcher_, [](uint32_t) {}, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
  io_handle_->close();

  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(errno, EBADF);
}

TEST_F(IoUringSocketHandleImplIntegrationTest, Accept) {
  initialize();

  // io_uring handle starts listening.
  bool accepted = false;
  auto local_addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  io_handle_->bind(local_addr);
  io_handle_->listen(5);
  io_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &accepted](uint32_t) {
        struct sockaddr addr;
        socklen_t addrlen = sizeof(addr);
        auto handle = io_handle_->accept(&addr, &addrlen);
        EXPECT_NE(handle, nullptr);
        accepted = true;
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Connect from peer handle.
  peer_io_handle_->connect(io_handle_->localAddress());

  while (!accepted) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_TRUE(accepted);

  // Close safely.
  io_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, ActivateReadEvent) {
  initialize();

  // io_uring handle starts listening.
  bool accepted = false;
  auto local_addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  io_handle_->bind(local_addr);
  io_handle_->listen(5);

  IoHandlePtr server_io_handler;
  io_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &accepted, &server_io_handler](uint32_t) {
        struct sockaddr addr;
        socklen_t addrlen = sizeof(addr);
        server_io_handler = io_handle_->accept(&addr, &addrlen);
        EXPECT_NE(server_io_handler, nullptr);
        accepted = true;
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Connect from peer handle.
  peer_io_handle_->connect(io_handle_->localAddress());

  while (!accepted) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_TRUE(accepted);

  Buffer::OwnedImpl read_buffer;

  server_io_handler->initializeFileEvent(
      *dispatcher_,
      [&server_io_handler, &read_buffer](uint32_t event) {
        EXPECT_EQ(event, Event::FileReadyType::Read);
        auto ret = server_io_handler->read(read_buffer, absl::nullopt);
        EXPECT_TRUE(ret.wouldBlock());
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  server_io_handler->activateFileEvents(Event::FileReadyType::Read);

  // Close safely.
  server_io_handler->close();
  io_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, Read) {
  initialize();

  // io_uring handle starts listening.
  bool accepted = false;
  auto local_addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  io_handle_->bind(local_addr);
  io_handle_->listen(5);

  IoHandlePtr server_io_handler;
  io_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &accepted, &server_io_handler](uint32_t) {
        struct sockaddr addr;
        socklen_t addrlen = sizeof(addr);
        server_io_handler = io_handle_->accept(&addr, &addrlen);
        EXPECT_NE(server_io_handler, nullptr);
        accepted = true;
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Connect from peer handle.
  peer_io_handle_->connect(io_handle_->localAddress());

  while (!accepted) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_TRUE(accepted);

  std::string data = "Hello world";
  Buffer::OwnedImpl write_buffer(data);
  Buffer::OwnedImpl read_buffer;

  server_io_handler->initializeFileEvent(
      *dispatcher_,
      [&server_io_handler, &read_buffer, &data](uint32_t event) {
        EXPECT_EQ(event, Event::FileReadyType::Read);
        auto ret = server_io_handler->read(read_buffer, absl::nullopt);
        EXPECT_EQ(ret.return_value_, data.size());
        // Read again would expect the EAGAIN returned.
        ret = server_io_handler->read(read_buffer, absl::nullopt);
        EXPECT_TRUE(ret.wouldBlock());
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  peer_io_handle_->write(write_buffer);

  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(read_buffer.toString(), data);

  server_io_handler->resetFileEvents();
  read_buffer.drain(read_buffer.length());
  EXPECT_EQ(read_buffer.length(), 0);

  bool first_read = true;

  server_io_handler->initializeFileEvent(
      *dispatcher_,
      [&server_io_handler, &read_buffer, &first_read](uint32_t event) {
        if (first_read) {
          EXPECT_EQ(event, Event::FileReadyType::Read);
          auto ret = server_io_handler->read(read_buffer, 5);
          EXPECT_EQ(ret.return_value_, 5);
        } else {
          EXPECT_EQ(event, Event::FileReadyType::Read);
          auto ret = server_io_handler->read(read_buffer, absl::nullopt);
        }
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  EXPECT_EQ(write_buffer.length(), 0);
  std::string data2 = "Hello world again";
  write_buffer.add(data2);
  peer_io_handle_->write(write_buffer);

  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(read_buffer.toString(), data2.substr(0, 5));

  // Cleanup previous read
  first_read = false;
  read_buffer.drain(read_buffer.length());
  EXPECT_EQ(write_buffer.length(), 0);

  // Write again to trigger the read event again.
  std::string data3 = " again";
  write_buffer.add(data3);
  peer_io_handle_->write(write_buffer);

  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Ensure we get the previous read and the new read
  EXPECT_EQ(read_buffer.toString(), data2.substr(5) + data3);

  // Close safely.
  server_io_handler->close();
  io_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, Readv) {
  initialize();

  // io_uring handle starts listening.
  bool accepted = false;
  auto local_addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  io_handle_->bind(local_addr);
  io_handle_->listen(5);

  IoHandlePtr server_io_handler;
  io_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &accepted, &server_io_handler](uint32_t) {
        struct sockaddr addr;
        socklen_t addrlen = sizeof(addr);
        server_io_handler = io_handle_->accept(&addr, &addrlen);
        EXPECT_NE(server_io_handler, nullptr);
        accepted = true;
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Connect from peer handle.
  peer_io_handle_->connect(io_handle_->localAddress());

  while (!accepted) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_TRUE(accepted);

  std::string data = "Hello world";
  Buffer::OwnedImpl write_buffer(data);
  Buffer::OwnedImpl read_buffer;

  server_io_handler->initializeFileEvent(
      *dispatcher_,
      [&server_io_handler, &read_buffer, &data](uint32_t event) {
        EXPECT_EQ(event, Event::FileReadyType::Read);
        Buffer::Reservation reservation = read_buffer.reserveForRead();
        auto ret = server_io_handler->readv(11, reservation.slices(), reservation.numSlices());
        EXPECT_EQ(ret.return_value_, data.size());
        reservation.commit(ret.return_value_);

        // Read again would expect the EAGAIN returned.
        Buffer::Reservation reservation2 = read_buffer.reserveForRead();
        ret = server_io_handler->readv(11, reservation2.slices(), reservation2.numSlices());
        EXPECT_TRUE(ret.wouldBlock());
        reservation2.commit(0);
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  peer_io_handle_->write(write_buffer);

  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(read_buffer.toString(), data);

  server_io_handler->resetFileEvents();
  read_buffer.drain(read_buffer.length());
  EXPECT_EQ(read_buffer.length(), 0);

  bool first_read = true;

  server_io_handler->initializeFileEvent(
      *dispatcher_,
      [&server_io_handler, &read_buffer, &first_read](uint32_t event) {
        if (first_read) {
          Buffer::Reservation reservation = read_buffer.reserveForRead();
          auto ret = server_io_handler->readv(5, reservation.slices(), reservation.numSlices());
          EXPECT_EQ(ret.return_value_, 5);
          reservation.commit(ret.return_value_);
        } else {
          EXPECT_EQ(event, Event::FileReadyType::Read);
          Buffer::Reservation reservation = read_buffer.reserveForRead();
          auto ret = server_io_handler->readv(1024, reservation.slices(), reservation.numSlices());
          reservation.commit(ret.return_value_);
        }
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  EXPECT_EQ(write_buffer.length(), 0);
  std::string data2 = "Hello world again";
  write_buffer.add(data2);
  peer_io_handle_->write(write_buffer);

  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(read_buffer.toString(), data2.substr(0, 5));

  // Cleanup previous read
  first_read = false;
  read_buffer.drain(read_buffer.length());
  EXPECT_EQ(write_buffer.length(), 0);

  // Write again to trigger the read event again.
  std::string data3 = " again";
  write_buffer.add(data3);
  peer_io_handle_->write(write_buffer);

  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Ensure we get the previous read and the new read
  EXPECT_EQ(read_buffer.toString(), data2.substr(5) + data3);

  // Close safely.
  server_io_handler->close();
  io_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, WriteAndWritev) {
  initialize();

  // io_uring handle starts listening.
  bool accepted = false;
  auto local_addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  io_handle_->bind(local_addr);
  io_handle_->listen(5);

  IoHandlePtr server_io_handler;
  io_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &accepted, &server_io_handler](uint32_t) {
        struct sockaddr addr;
        socklen_t addrlen = sizeof(addr);
        server_io_handler = io_handle_->accept(&addr, &addrlen);
        EXPECT_NE(server_io_handler, nullptr);
        accepted = true;
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Connect from peer handle.
  peer_io_handle_->connect(io_handle_->localAddress());

  while (!accepted) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_TRUE(accepted);

  std::string data = "Hello world";
  Buffer::OwnedImpl write_buffer(data);
  Buffer::OwnedImpl read_buffer;

  server_io_handler->initializeFileEvent(
      *dispatcher_, [](uint32_t) {}, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Write);

  peer_io_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &read_buffer, &data](uint32_t event) {
        EXPECT_EQ(event, Event::FileReadyType::Read);
        auto ret = peer_io_handle_->read(read_buffer, absl::nullopt);
        EXPECT_EQ(ret.return_value_, data.size());
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  EXPECT_EQ(data.size(), server_io_handler->write(write_buffer).return_value_);

  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(read_buffer.toString(), data);
  EXPECT_EQ(write_buffer.length(), 0);
  read_buffer.drain(data.size());

  // Test another write interface
  write_buffer.add(data);
  auto slices = write_buffer.getRawSlices();
  EXPECT_EQ(data.size(), server_io_handler->writev(&slices[0], 1).return_value_);

  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(read_buffer.toString(), data);
  peer_io_handle_->resetFileEvents();

  peer_io_handle_->close();
  server_io_handler->close();
  io_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, ActivateWriteEvent) {
  initialize();

  // io_uring handle starts listening.
  bool accepted = false;
  auto local_addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  io_handle_->bind(local_addr);
  io_handle_->listen(5);

  IoHandlePtr server_io_handler;
  io_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &accepted, &server_io_handler](uint32_t) {
        struct sockaddr addr;
        socklen_t addrlen = sizeof(addr);
        server_io_handler = io_handle_->accept(&addr, &addrlen);
        EXPECT_NE(server_io_handler, nullptr);
        accepted = true;
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Connect from peer handle.
  peer_io_handle_->connect(io_handle_->localAddress());

  while (!accepted) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_TRUE(accepted);

  Buffer::OwnedImpl write_buffer;

  server_io_handler->initializeFileEvent(
      *dispatcher_,
      [&server_io_handler, &write_buffer](uint32_t event) {
        EXPECT_EQ(event, Event::FileReadyType::Write);
        auto ret = server_io_handler->write(write_buffer);
        EXPECT_TRUE(ret.wouldBlock());
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Write);

  server_io_handler->activateFileEvents(Event::FileReadyType::Write);

  // Close safely.
  server_io_handler->close();
  io_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, Recv) {
  initialize();

  // io_uring handle starts listening.
  bool accepted = false;
  auto local_addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  io_handle_->bind(local_addr);
  io_handle_->listen(5);

  IoHandlePtr server_io_handler;
  io_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &accepted, &server_io_handler](uint32_t) {
        struct sockaddr addr;
        socklen_t addrlen = sizeof(addr);
        server_io_handler = io_handle_->accept(&addr, &addrlen);
        EXPECT_NE(server_io_handler, nullptr);
        accepted = true;
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Connect from peer handle.
  peer_io_handle_->connect(io_handle_->localAddress());

  while (!accepted) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_TRUE(accepted);

  std::string data = "Hello world";
  Buffer::OwnedImpl write_buffer(data);
  Buffer::OwnedImpl peek_buffer;
  Buffer::OwnedImpl recv_buffer;

  server_io_handler->initializeFileEvent(
      *dispatcher_,
      [&server_io_handler, &peek_buffer, &recv_buffer, &data](uint32_t event) {
        EXPECT_EQ(event, Event::FileReadyType::Read);
        // Recv with MSG_PEEK will not drain the buffer.
        Buffer::Reservation reservation = peek_buffer.reserveForRead();
        auto ret = server_io_handler->recv(reservation.slices()->mem_, 5, MSG_PEEK);
        EXPECT_EQ(ret.return_value_, 5);
        reservation.commit(ret.return_value_);

        // Recv without flags behaves the same as readv.
        Buffer::Reservation reservation2 = recv_buffer.reserveForRead();
        auto ret2 = server_io_handler->recv(reservation2.slices()->mem_, reservation2.length(), 0);
        EXPECT_EQ(ret2.return_value_, data.size());
        reservation2.commit(ret2.return_value_);
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  peer_io_handle_->write(write_buffer);

  while (recv_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(peek_buffer.toString(), "Hello");
  EXPECT_EQ(recv_buffer.toString(), data);

  // Close safely.
  server_io_handler->close();
  io_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

// This tests the case of a write event will be emitted after remote closed when the read is
// disabled and only write event is listened
TEST_F(IoUringSocketHandleImplIntegrationTest, RemoteCloseWithoutEnableCloseEventAndDisabled) {
  initialize();

  // io_uring handle starts listening.
  bool accepted = false;
  auto local_addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  io_handle_->bind(local_addr);
  io_handle_->listen(5);

  IoHandlePtr server_io_handler;
  io_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &accepted, &server_io_handler](uint32_t) {
        struct sockaddr addr;
        socklen_t addrlen = sizeof(addr);
        server_io_handler = io_handle_->accept(&addr, &addrlen);
        EXPECT_NE(server_io_handler, nullptr);
        accepted = true;
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Connect from peer handle.
  peer_io_handle_->connect(io_handle_->localAddress());

  while (!accepted) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_TRUE(accepted);

  Buffer::OwnedImpl read_buffer;
  bool got_write_event = false;

  server_io_handler->initializeFileEvent(
      *dispatcher_,
      [&server_io_handler, &read_buffer, &got_write_event](uint32_t event) {
        EXPECT_EQ(event, Event::FileReadyType::Write);
        auto ret = server_io_handler->read(read_buffer, absl::nullopt);
        EXPECT_TRUE(ret.ok());
        EXPECT_EQ(0, ret.return_value_);
        server_io_handler->close();
        got_write_event = true;
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  server_io_handler->enableFileEvents(Event::FileReadyType::Write);

  peer_io_handle_->close();

  while (!got_write_event) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(read_buffer.length(), 0);

  // Close safely.
  io_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, RemoteCloseWithoutEnableCloseEvent) {
  initialize();

  // io_uring handle starts listening.
  bool accepted = false;
  auto local_addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  io_handle_->bind(local_addr);
  io_handle_->listen(5);

  IoHandlePtr server_io_handler;
  io_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &accepted, &server_io_handler](uint32_t) {
        struct sockaddr addr;
        socklen_t addrlen = sizeof(addr);
        server_io_handler = io_handle_->accept(&addr, &addrlen);
        EXPECT_NE(server_io_handler, nullptr);
        accepted = true;
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Connect from peer handle.
  peer_io_handle_->connect(io_handle_->localAddress());

  while (!accepted) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_TRUE(accepted);

  std::string data = "Hello world";
  Buffer::OwnedImpl write_buffer(data);
  Buffer::OwnedImpl read_buffer;

  server_io_handler->initializeFileEvent(
      *dispatcher_,
      [&server_io_handler, &read_buffer, &data](uint32_t event) {
        EXPECT_EQ(event, Event::FileReadyType::Read);
        auto ret = server_io_handler->read(read_buffer, absl::nullopt);
        if (ret.return_value_ > 0) {
          EXPECT_EQ(ret.return_value_, data.size());
          // Read again would expect the EAGAIN returned.
          ret = server_io_handler->read(read_buffer, absl::nullopt);
          EXPECT_TRUE(ret.wouldBlock());
          return;
        }
        if (ret.return_value_ == 0) {
          server_io_handler->close();
        }
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  peer_io_handle_->write(write_buffer);
  peer_io_handle_->close();

  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(read_buffer.toString(), data);

  // Close safely.
  io_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, RemoteCloseWithEnableCloseEvent) {
  initialize();

  // io_uring handle starts listening.
  bool accepted = false;
  auto local_addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  io_handle_->bind(local_addr);
  io_handle_->listen(5);

  IoHandlePtr server_io_handler;
  io_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &accepted, &server_io_handler](uint32_t) {
        struct sockaddr addr;
        socklen_t addrlen = sizeof(addr);
        server_io_handler = io_handle_->accept(&addr, &addrlen);
        EXPECT_NE(server_io_handler, nullptr);
        accepted = true;
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Connect from peer handle.
  peer_io_handle_->connect(io_handle_->localAddress());

  while (!accepted) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_TRUE(accepted);

  std::string data = "Hello world";
  Buffer::OwnedImpl write_buffer(data);
  Buffer::OwnedImpl read_buffer;

  server_io_handler->initializeFileEvent(
      *dispatcher_,
      [&server_io_handler, &read_buffer, &data](uint32_t event) {
        if (event & Event::FileReadyType::Read) {
          auto ret = server_io_handler->read(read_buffer, absl::nullopt);
          EXPECT_EQ(ret.return_value_, data.size());
          // Read again would expect the EAGAIN returned.
          ret = server_io_handler->read(read_buffer, absl::nullopt);
          EXPECT_TRUE(ret.wouldBlock());
          return;
        }
        if (event & Event::FileReadyType::Read) {
          auto ret = server_io_handler->read(read_buffer, absl::nullopt);
          EXPECT_EQ(0, ret.return_value_);
          server_io_handler->close();
        }
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read | Event::FileReadyType::Closed);

  peer_io_handle_->write(write_buffer);
  peer_io_handle_->close();

  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(read_buffer.toString(), data);

  // Close safely.
  io_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

// This test ensure IoUringHandleImpl will close the socket when destructing.
TEST_F(IoUringSocketHandleImplIntegrationTest, CloseIoUringSocketWhenDestructing) {
  initialize();

  // io_uring handle starts listening.
  bool accepted = false;
  auto local_addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  io_handle_->bind(local_addr);
  io_handle_->listen(5);

  IoHandlePtr server_io_handler;
  io_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &accepted, &server_io_handler](uint32_t) {
        struct sockaddr addr;
        socklen_t addrlen = sizeof(addr);
        server_io_handler = io_handle_->accept(&addr, &addrlen);
        EXPECT_NE(server_io_handler, nullptr);
        accepted = true;
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Connect from peer handle.
  peer_io_handle_->connect(io_handle_->localAddress());

  while (!accepted) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_TRUE(accepted);

  std::string data = "Hello world";
  Buffer::OwnedImpl write_buffer(data);
  Buffer::OwnedImpl read_buffer;

  server_io_handler->initializeFileEvent(
      *dispatcher_,
      [&server_io_handler, &read_buffer, &data](uint32_t event) {
        EXPECT_EQ(event, Event::FileReadyType::Read);
        auto ret = server_io_handler->read(read_buffer, absl::nullopt);
        EXPECT_EQ(ret.return_value_, data.size());
        // Read again would expect the EAGAIN returned.
        ret = server_io_handler->read(read_buffer, absl::nullopt);
        EXPECT_TRUE(ret.wouldBlock());
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  peer_io_handle_->write(write_buffer);

  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(read_buffer.toString(), data);

  server_io_handler.reset();

  while (peer_io_handle_->read(read_buffer, absl::nullopt).return_value_ != 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  // Close safely.
  io_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

// This test ensure IoUringHandleImpl can be released correctly when the IoUringWorker
// is released early than it.
TEST_F(IoUringSocketHandleImplIntegrationTest, ReleaseIoUringWorkerEarlyThanIohandle) {
  initialize();

  // io_uring handle starts listening.
  auto local_addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  io_handle_->bind(local_addr);
  io_handle_->listen(5);
  io_handle_->initializeFileEvent(
      *dispatcher_, [](uint32_t) {}, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  thread_is_shutdown_ = true;
  instance_.shutdownGlobalThreading();
  instance_.shutdownThread();
}

TEST_F(IoUringSocketHandleImplIntegrationTest, MigrateServerSocketBetweenThread) {
  initialize(true);

  // io_uring handle starts listening.
  bool accepted = false;
  auto local_addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  io_handle_->bind(local_addr);
  io_handle_->listen(5);

  IoHandlePtr server_io_handler;
  io_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &accepted, &server_io_handler](uint32_t) {
        struct sockaddr addr;
        socklen_t addrlen = sizeof(addr);
        server_io_handler = io_handle_->accept(&addr, &addrlen);
        EXPECT_NE(server_io_handler, nullptr);
        accepted = true;
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Connect from peer handle.
  peer_io_handle_->connect(io_handle_->localAddress());

  while (!accepted) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_TRUE(accepted);

  std::string data = "Hello world";
  Buffer::OwnedImpl write_buffer(data);
  Buffer::OwnedImpl read_buffer;

  server_io_handler->initializeFileEvent(
      *dispatcher_,
      [&server_io_handler, &read_buffer, &data](uint32_t event) {
        EXPECT_EQ(event, Event::FileReadyType::Read);
        auto ret = server_io_handler->read(read_buffer, absl::nullopt);
        EXPECT_EQ(ret.return_value_, data.size());
        // Read again would expect the EAGAIN returned.
        ret = server_io_handler->read(read_buffer, absl::nullopt);
        EXPECT_TRUE(ret.wouldBlock());
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  peer_io_handle_->write(write_buffer);

  while (read_buffer.length() == 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(read_buffer.toString(), data);

  server_io_handler->resetFileEvents();
  read_buffer.drain(read_buffer.length());
  EXPECT_EQ(read_buffer.length(), 0);
  std::atomic<bool> read_done = false;
  std::atomic<bool> initialized_in_new_thread = false;

  second_dispatcher_->post([&server_io_handler, &second_dispatcher = second_dispatcher_,
                            &read_buffer, &read_done, &data, &initialized_in_new_thread]() {
    server_io_handler->initializeFileEvent(
        *second_dispatcher,
        [&server_io_handler, &read_buffer, &data, &read_done](uint32_t event) {
          EXPECT_EQ(event, Event::FileReadyType::Read);
          auto ret = server_io_handler->read(read_buffer, absl::nullopt);
          EXPECT_EQ(ret.return_value_, data.size());
          // Read again would expect the EAGAIN returned.
          ret = server_io_handler->read(read_buffer, absl::nullopt);
          EXPECT_TRUE(ret.wouldBlock());
          read_done = true;
          server_io_handler->close();
        },
        Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
    initialized_in_new_thread = true;
  });

  while (!initialized_in_new_thread) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  write_buffer.add(data);
  peer_io_handle_->write(write_buffer);

  while (!read_done) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    sleep(1);
  }
  EXPECT_EQ(read_buffer.toString(), data);

  second_dispatcher_->exit();
  second_thread_->join();
  io_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, Connect) {
  initialize();

  // Peer handle starts listening.
  auto local_addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  peer_io_handle_->bind(local_addr);
  peer_io_handle_->listen(5);

  struct sockaddr addr;
  socklen_t addrlen = sizeof(addr);
  peer_io_handle_->accept(&addr, &addrlen);

  int error = -1;
  socklen_t error_size = sizeof(error);
  io_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &error, &error_size](uint32_t events) {
        if (events & Event::FileReadyType::Write) {
          io_handle_->getOption(SOL_SOCKET, SO_ERROR, &error, &error_size);
        }
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Connect from io_uring handle.
  io_handle_->connect(peer_io_handle_->localAddress());

  while (error == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(error, 0);

  // Close safely.
  io_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(IoUringSocketHandleImplIntegrationTest, ConnectError) {
  initialize();

  auto local_addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 9999);

  int original_error = -1;
  socklen_t original_error_size = sizeof(original_error);
  int error = -1;
  socklen_t error_size = sizeof(error);
  io_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &error, &error_size, &original_error, &original_error_size](uint32_t events) {
        if (events & Event::FileReadyType::Write) {
          getsockopt(io_handle_->fdDoNotUse(), SOL_SOCKET, SO_ERROR, &original_error,
                     &original_error_size);
          io_handle_->getOption(SOL_SOCKET, SO_ERROR, &error, &error_size);
        }
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Connect from io_uring handle.
  io_handle_->connect(local_addr);

  while (error == -1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(original_error, 0);
  EXPECT_EQ(error, ECONNREFUSED);

  // Close safely.
  io_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

} // namespace
} // namespace Network
} // namespace Envoy
