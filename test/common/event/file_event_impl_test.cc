#include <cstdint>

#include "envoy/event/file_event.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/stats/isolated_store_impl.h"

#include "test/mocks/common.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Event {
namespace {

class FileEventImplTest : public testing::Test {
public:
  FileEventImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")),
        os_sys_calls_(Api::OsSysCallsSingleton::get()) {}

  void SetUp() override {
#ifdef WIN32
    ASSERT_EQ(0, os_sys_calls_.socketpair(AF_INET, SOCK_STREAM, 0, fds_).return_value_);
#else
    ASSERT_EQ(0, os_sys_calls_.socketpair(AF_UNIX, SOCK_DGRAM, 0, fds_).return_value_);
#endif
    int data = 1;

    const Api::SysCallSizeResult result = os_sys_calls_.write(fds_[1], &data, sizeof(data));
    ASSERT_EQ(sizeof(data), static_cast<size_t>(result.return_value_));
  }

  void clearReadable() {
    // Read the data from the socket so it is no longer readable.
    char buffer[10];
    struct iovec vec {
      buffer, sizeof(buffer)
    };
    const Api::SysCallSizeResult result = os_sys_calls_.readv(fds_[0], &vec, 1);
    EXPECT_LT(0, static_cast<size_t>(result.return_value_));
    EXPECT_GT(sizeof(buffer), static_cast<size_t>(result.return_value_));
  }

  void TearDown() override {
    os_sys_calls_.close(fds_[0]);
    os_sys_calls_.close(fds_[1]);
  }

protected:
  os_fd_t fds_[2];
  Api::ApiPtr api_;
  DispatcherPtr dispatcher_;
  Api::OsSysCalls& os_sys_calls_;
};

class FileEventImplActivateTest : public testing::TestWithParam<Network::Address::IpVersion> {
public:
  FileEventImplActivateTest() : os_sys_calls_(Api::OsSysCallsSingleton::get()) {}

  static void onWatcherReady(evwatch*, const evwatch_prepare_cb_info*, void* arg) {
    // `arg` contains the ReadyWatcher passed in from evwatch_prepare_new.
    auto watcher = static_cast<ReadyWatcher*>(arg);
    watcher->ready();
  }

  int domain() { return GetParam() == Network::Address::IpVersion::v4 ? AF_INET : AF_INET6; }

protected:
  Api::OsSysCalls& os_sys_calls_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, FileEventImplActivateTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(FileEventImplActivateTest, Activate) {
  os_fd_t fd = os_sys_calls_.socket(domain(), SOCK_STREAM, 0).return_value_;
  ASSERT_TRUE(SOCKET_VALID(fd));

  Api::ApiPtr api = Api::createApiForTest();
  DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  ReadyWatcher read_event;
  EXPECT_CALL(read_event, ready());
  ReadyWatcher write_event;
  EXPECT_CALL(write_event, ready());

  const FileTriggerType trigger = Event::PlatformDefaultTriggerType;

  Event::FileEventPtr file_event = dispatcher->createFileEvent(
      fd,
      [&](uint32_t events) {
        if (events & FileReadyType::Read) {
          read_event.ready();
        }

        if (events & FileReadyType::Write) {
          write_event.ready();
        }
        return absl::OkStatus();
      },
      trigger, FileReadyType::Read | FileReadyType::Write);

  file_event->activate(FileReadyType::Read | FileReadyType::Write);
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);

  os_sys_calls_.close(fd);
}

TEST_P(FileEventImplActivateTest, ActivateChaining) {
  os_fd_t fd = os_sys_calls_.socket(domain(), SOCK_DGRAM, 0).return_value_;
  ASSERT_TRUE(SOCKET_VALID(fd));

  Api::ApiPtr api = Api::createApiForTest();
  DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  ReadyWatcher fd_event;
  ReadyWatcher read_event;
  ReadyWatcher write_event;

  ReadyWatcher prepare_watcher;
  evwatch_prepare_new(&static_cast<DispatcherImpl*>(dispatcher.get())->base(), onWatcherReady,
                      &prepare_watcher);

  const FileTriggerType trigger = Event::PlatformDefaultTriggerType;

  Event::FileEventPtr file_event = dispatcher->createFileEvent(
      fd,
      [&](uint32_t events) {
        fd_event.ready();
        if (events & FileReadyType::Read) {
          read_event.ready();
          file_event->activate(FileReadyType::Write);
        }

        if (events & FileReadyType::Write) {
          write_event.ready();
        }
        return absl::OkStatus();
      },
      trigger, FileReadyType::Read | FileReadyType::Write);

  testing::InSequence s;
  // First loop iteration: handle scheduled read event and the real write event produced by poll.
  // Note that the real and injected events are combined and delivered in a single call to the fd
  // callback.
  EXPECT_CALL(prepare_watcher, ready());
  EXPECT_CALL(fd_event, ready());
  EXPECT_CALL(read_event, ready());
  EXPECT_CALL(write_event, ready());
  // Second loop iteration: handle write and close events scheduled while handling read.
  EXPECT_CALL(prepare_watcher, ready());
  EXPECT_CALL(fd_event, ready());
  EXPECT_CALL(write_event, ready());
  if constexpr (Event::PlatformDefaultTriggerType != Event::FileTriggerType::EmulatedEdge) {
    // Third loop iteration: poll returned no new real events.
    EXPECT_CALL(prepare_watcher, ready());
  }

  file_event->activate(FileReadyType::Read);
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);

  os_sys_calls_.close(fd);
}

TEST_P(FileEventImplActivateTest, SetEnableCancelsActivate) {
  os_fd_t fd = os_sys_calls_.socket(domain(), SOCK_DGRAM, 0).return_value_;
  ASSERT_TRUE(SOCKET_VALID(fd));

  Api::ApiPtr api = Api::createApiForTest();
  DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  ReadyWatcher fd_event;
  ReadyWatcher read_event;
  ReadyWatcher write_event;

  ReadyWatcher prepare_watcher;
  evwatch_prepare_new(&static_cast<DispatcherImpl*>(dispatcher.get())->base(), onWatcherReady,
                      &prepare_watcher);

  const FileTriggerType trigger = Event::PlatformDefaultTriggerType;

  Event::FileEventPtr file_event = dispatcher->createFileEvent(
      fd,
      [&](uint32_t events) {
        fd_event.ready();
        if (events & FileReadyType::Read) {
          read_event.ready();
          file_event->activate(FileReadyType::Closed);
          file_event->setEnabled(FileReadyType::Write | FileReadyType::Closed);
        }

        if (events & FileReadyType::Write) {
          write_event.ready();
        }
        return absl::OkStatus();
      },
      trigger, FileReadyType::Read | FileReadyType::Write);

  testing::InSequence s;
  // First loop iteration: handle scheduled read event and the real write event produced by poll.
  // Note that the real and injected events are combined and delivered in a single call to the fd
  // callback.
  EXPECT_CALL(prepare_watcher, ready());
  EXPECT_CALL(fd_event, ready());
  EXPECT_CALL(read_event, ready());
  EXPECT_CALL(write_event, ready());
  // Second loop iteration: handle real write event after resetting event mask via setEnabled. Close
  // injected event is discarded by the setEnable call.
  EXPECT_CALL(prepare_watcher, ready());
  EXPECT_CALL(fd_event, ready());
  EXPECT_CALL(write_event, ready());
  // Third loop iteration: poll returned no new real events.
  EXPECT_CALL(prepare_watcher, ready());

  file_event->activate(FileReadyType::Read);
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);

  os_sys_calls_.close(fd);
}

#ifndef WIN32 // Libevent on Windows doesn't support edge trigger.
TEST_F(FileEventImplTest, EdgeTrigger) {
  ReadyWatcher read_event;
  EXPECT_CALL(read_event, ready());
  ReadyWatcher write_event;
  EXPECT_CALL(write_event, ready());

  Event::FileEventPtr file_event = dispatcher_->createFileEvent(
      fds_[0],
      [&](uint32_t events) {
        if (events & FileReadyType::Read) {
          read_event.ready();
        }

        if (events & FileReadyType::Write) {
          write_event.ready();
        }
        return absl::OkStatus();
      },
      FileTriggerType::Edge, FileReadyType::Read | FileReadyType::Write);

  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}
#endif

TEST_F(FileEventImplTest, LevelTrigger) {
  testing::InSequence s;
  ReadyWatcher read_event;
  ReadyWatcher write_event;

  int count = 0;
  Event::FileEventPtr file_event = dispatcher_->createFileEvent(
      fds_[0],
      [&](uint32_t events) {
        ASSERT(count > 0);
        if (--count == 0) {
          dispatcher_->exit();
        }
        if (events & FileReadyType::Read) {
          read_event.ready();
        }

        if (events & FileReadyType::Write) {
          write_event.ready();
        }
        return absl::OkStatus();
      },
      FileTriggerType::Level, FileReadyType::Read | FileReadyType::Write);

  // Expect events to be delivered twice since count=2 and level events are delivered on each
  // iteration until the fd state changes.
  EXPECT_CALL(read_event, ready());
  EXPECT_CALL(write_event, ready());
  EXPECT_CALL(read_event, ready());
  EXPECT_CALL(write_event, ready());
  count = 2;
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Change the event mask to just Write and verify that only that event is delivered.
  EXPECT_CALL(read_event, ready()).Times(0);
  EXPECT_CALL(write_event, ready());
  file_event->setEnabled(FileReadyType::Write);
  count = 1;
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Activate read, and verify it is delivered despite not being part of the enabled event mask.
  EXPECT_CALL(read_event, ready());
  EXPECT_CALL(write_event, ready());
  file_event->activate(FileReadyType::Read);
  count = 1;
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Activate read and then call setEnabled. Verify that the read event is not delivered; setEnabled
  // clears events from explicit calls to activate.
  EXPECT_CALL(read_event, ready()).Times(0);
  EXPECT_CALL(write_event, ready());
  file_event->activate(FileReadyType::Read);
  file_event->setEnabled(FileReadyType::Write);
  count = 1;
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_F(FileEventImplTest, SetEnabled) {
  testing::InSequence s;
  ReadyWatcher read_event;
  ReadyWatcher write_event;

  const FileTriggerType trigger = Event::PlatformDefaultTriggerType;

  Event::FileEventPtr file_event = dispatcher_->createFileEvent(
      fds_[0],
      [&](uint32_t events) {
        if (events & FileReadyType::Read) {
          read_event.ready();
        }

        if (events & FileReadyType::Write) {
          write_event.ready();
        }
        return absl::OkStatus();
      },
      trigger, FileReadyType::Read | FileReadyType::Write);

  EXPECT_CALL(read_event, ready());
  file_event->setEnabled(FileReadyType::Read);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(write_event, ready());
  file_event->setEnabled(FileReadyType::Write);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  file_event->setEnabled(0);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(read_event, ready());
  EXPECT_CALL(write_event, ready());
  file_event->setEnabled(FileReadyType::Read | FileReadyType::Write);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Run a manual event to ensure that previous expectations are satisfied before moving on.
  ReadyWatcher manual_event;
  EXPECT_CALL(manual_event, ready());
  manual_event.ready();

  clearReadable();

  file_event->setEnabled(FileReadyType::Read);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(write_event, ready());
  file_event->setEnabled(FileReadyType::Write);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(write_event, ready());
  file_event->setEnabled(FileReadyType::Read | FileReadyType::Write);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Repeat the previous registration, verify that write event is delivered again.
  EXPECT_CALL(write_event, ready());
  file_event->setEnabled(FileReadyType::Read | FileReadyType::Write);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Synthetic read events are delivered even if the active registration doesn't contain them.
  EXPECT_CALL(read_event, ready());
  file_event->activate(FileReadyType::Read);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Run a manual event to ensure that previous expectations are satisfied before moving on.
  EXPECT_CALL(manual_event, ready());
  manual_event.ready();

  // Do a read activation followed setEnabled to verify that the activation is cleared.
  EXPECT_CALL(write_event, ready());
  file_event->activate(FileReadyType::Read);
  file_event->setEnabled(FileReadyType::Read | FileReadyType::Write);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Repeat the previous steps but with the same input to setEnabled to verify that the activation
  // is cleared even in cases where the setEnable mask hasn't changed.
  EXPECT_CALL(write_event, ready());
  file_event->activate(FileReadyType::Read);
  file_event->setEnabled(FileReadyType::Read | FileReadyType::Write);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

TEST_F(FileEventImplTest, RegisterIfEmulatedEdge) {
  // Test only applies if using EmulatedEdge trigger mode
  if constexpr (PlatformDefaultTriggerType != FileTriggerType::EmulatedEdge) {
    return;
  }

  testing::InSequence s;
  ReadyWatcher read_event;
  ReadyWatcher write_event;

  const FileTriggerType trigger = Event::PlatformDefaultTriggerType;

  Event::FileEventPtr file_event = dispatcher_->createFileEvent(
      fds_[0],
      [&](uint32_t events) {
        if (events & FileReadyType::Read) {
          read_event.ready();
        }

        if (events & FileReadyType::Write) {
          write_event.ready();
        }
        return absl::OkStatus();
      },
      trigger, FileReadyType::Read | FileReadyType::Write);

  EXPECT_CALL(read_event, ready()).Times(0);
  EXPECT_CALL(write_event, ready()).Times(0);
  file_event->unregisterEventIfEmulatedEdge(FileReadyType::Read | FileReadyType::Write);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(read_event, ready());
  file_event->registerEventIfEmulatedEdge(FileReadyType::Read);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(write_event, ready());
  file_event->registerEventIfEmulatedEdge(FileReadyType::Write);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(read_event, ready());
  file_event->registerEventIfEmulatedEdge(FileReadyType::Read | FileReadyType::Write);
  file_event->unregisterEventIfEmulatedEdge(FileReadyType::Write);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(read_event, ready()).Times(0);
  EXPECT_CALL(write_event, ready()).Times(0);
  file_event->unregisterEventIfEmulatedEdge(FileReadyType::Read);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(read_event, ready());
  EXPECT_CALL(write_event, ready());
  file_event->registerEventIfEmulatedEdge(FileReadyType::Read | FileReadyType::Write);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Events are delivered once due to auto unregistration after they are delivered.
  EXPECT_CALL(read_event, ready()).Times(0);
  EXPECT_CALL(write_event, ready()).Times(0);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

} // namespace
} // namespace Event
} // namespace Envoy
