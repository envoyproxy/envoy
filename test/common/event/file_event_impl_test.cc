#include <cstdint>

#include "envoy/event/file_event.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/common.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
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
    ASSERT_EQ(0, os_sys_calls_.socketpair(AF_INET, SOCK_STREAM, 0, fds_).rc_);
#else
    ASSERT_EQ(0, os_sys_calls_.socketpair(AF_UNIX, SOCK_DGRAM, 0, fds_).rc_);
#endif
    int data = 1;

    const Api::SysCallSizeResult result = os_sys_calls_.write(fds_[1], &data, sizeof(data));
    ASSERT_EQ(sizeof(data), static_cast<size_t>(result.rc_));
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

class FileEventImplActivateTest
    : public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>> {
public:
  FileEventImplActivateTest() : os_sys_calls_(Api::OsSysCallsSingleton::get()) {
    Runtime::LoaderSingleton::getExisting()->mergeValues(
        {{"envoy.reloadable_features.activate_fds_next_event_loop",
          activateFdsNextEventLoop() ? "true" : "false"}});
  }

  static void onWatcherReady(evwatch*, const evwatch_prepare_cb_info*, void* arg) {
    // `arg` contains the ReadyWatcher passed in from evwatch_prepare_new.
    auto watcher = static_cast<ReadyWatcher*>(arg);
    watcher->ready();
  }

  int domain() {
    return std::get<0>(GetParam()) == Network::Address::IpVersion::v4 ? AF_INET : AF_INET6;
  }
  bool activateFdsNextEventLoop() { return std::get<1>(GetParam()); }

protected:
  Api::OsSysCalls& os_sys_calls_;
  TestScopedRuntime scoped_runtime_;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersions, FileEventImplActivateTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()), testing::Bool()));

TEST_P(FileEventImplActivateTest, Activate) {
  os_fd_t fd = os_sys_calls_.socket(domain(), SOCK_STREAM, 0).rc_;
  ASSERT_TRUE(SOCKET_VALID(fd));

  Api::ApiPtr api = Api::createApiForTest();
  DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  ReadyWatcher read_event;
  EXPECT_CALL(read_event, ready()).Times(1);
  ReadyWatcher write_event;
  EXPECT_CALL(write_event, ready()).Times(1);
  ReadyWatcher closed_event;
  EXPECT_CALL(closed_event, ready()).Times(1);

#ifdef WIN32
  const FileTriggerType trigger = FileTriggerType::Level;
#else
  const FileTriggerType trigger = FileTriggerType::Edge;
#endif

  Event::FileEventPtr file_event = dispatcher->createFileEvent(
      fd,
      [&](uint32_t events) -> void {
        if (events & FileReadyType::Read) {
          read_event.ready();
        }

        if (events & FileReadyType::Write) {
          write_event.ready();
        }

        if (events & FileReadyType::Closed) {
          closed_event.ready();
        }
      },
      trigger, FileReadyType::Read | FileReadyType::Write | FileReadyType::Closed);

  file_event->activate(FileReadyType::Read | FileReadyType::Write | FileReadyType::Closed);
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);

  os_sys_calls_.close(fd);
}

TEST_P(FileEventImplActivateTest, ActivateChaining) {
  os_fd_t fd = os_sys_calls_.socket(domain(), SOCK_STREAM, 0).rc_;
  ASSERT_TRUE(SOCKET_VALID(fd));

  Api::ApiPtr api = Api::createApiForTest();
  DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  ReadyWatcher fd_event;
  ReadyWatcher read_event;
  ReadyWatcher write_event;
  ReadyWatcher closed_event;

  ReadyWatcher prepare_watcher;
  evwatch_prepare_new(&static_cast<DispatcherImpl*>(dispatcher.get())->base(), onWatcherReady,
                      &prepare_watcher);

#ifdef WIN32
  const FileTriggerType trigger = FileTriggerType::Level;
#else
  const FileTriggerType trigger = FileTriggerType::Edge;
#endif

  Event::FileEventPtr file_event = dispatcher->createFileEvent(
      fd,
      [&](uint32_t events) -> void {
        fd_event.ready();
        if (events & FileReadyType::Read) {
          read_event.ready();
          file_event->activate(FileReadyType::Write);
          file_event->activate(FileReadyType::Closed);
        }

        if (events & FileReadyType::Write) {
          write_event.ready();
          file_event->activate(FileReadyType::Closed);
        }

        if (events & FileReadyType::Closed) {
          closed_event.ready();
        }
      },
      trigger, FileReadyType::Read | FileReadyType::Write | FileReadyType::Closed);

  testing::InSequence s;
  // First loop iteration: handle scheduled read event and the real write event produced by poll.
  // Note that the real and injected events are combined and delivered in a single call to the fd
  // callback.
  EXPECT_CALL(prepare_watcher, ready());
  EXPECT_CALL(fd_event, ready());
  EXPECT_CALL(read_event, ready());
  EXPECT_CALL(write_event, ready());
  if (activateFdsNextEventLoop()) {
    // Second loop iteration: handle write and close events scheduled while handling read.
    EXPECT_CALL(prepare_watcher, ready());
    EXPECT_CALL(fd_event, ready());
    EXPECT_CALL(write_event, ready());
    EXPECT_CALL(closed_event, ready());
    // Third loop iteration: handle close event scheduled while handling write.
    EXPECT_CALL(prepare_watcher, ready());
    EXPECT_CALL(fd_event, ready());
    EXPECT_CALL(closed_event, ready());
    // Fourth loop iteration: poll returned no new real events.
    EXPECT_CALL(prepare_watcher, ready());
  } else {
    // Same loop iteration activation: handle write and close events scheduled while handling read.
    EXPECT_CALL(fd_event, ready());
    EXPECT_CALL(write_event, ready());
    EXPECT_CALL(closed_event, ready());
    // Second same loop iteration activation: handle close event scheduled while handling write.
    EXPECT_CALL(fd_event, ready());
    EXPECT_CALL(closed_event, ready());
    // Second loop iteration: poll returned no new real events.
    EXPECT_CALL(prepare_watcher, ready());
  }

  file_event->activate(FileReadyType::Read);
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);

  os_sys_calls_.close(fd);
}

TEST_P(FileEventImplActivateTest, SetEnableCancelsActivate) {
  os_fd_t fd = os_sys_calls_.socket(domain(), SOCK_STREAM, 0).rc_;
  ASSERT_TRUE(SOCKET_VALID(fd));

  Api::ApiPtr api = Api::createApiForTest();
  DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  ReadyWatcher fd_event;
  ReadyWatcher read_event;
  ReadyWatcher write_event;
  ReadyWatcher closed_event;

  ReadyWatcher prepare_watcher;
  evwatch_prepare_new(&static_cast<DispatcherImpl*>(dispatcher.get())->base(), onWatcherReady,
                      &prepare_watcher);

#ifdef WIN32
  const FileTriggerType trigger = FileTriggerType::Level;
#else
  const FileTriggerType trigger = FileTriggerType::Edge;
#endif

  Event::FileEventPtr file_event = dispatcher->createFileEvent(
      fd,
      [&](uint32_t events) -> void {
        fd_event.ready();
        if (events & FileReadyType::Read) {
          read_event.ready();
          file_event->activate(FileReadyType::Closed);
          file_event->setEnabled(FileReadyType::Write | FileReadyType::Closed);
        }

        if (events & FileReadyType::Write) {
          write_event.ready();
        }

        if (events & FileReadyType::Closed) {
          closed_event.ready();
        }
      },
      trigger, FileReadyType::Read | FileReadyType::Write | FileReadyType::Closed);

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
  EXPECT_CALL(read_event, ready()).Times(1);
  ReadyWatcher write_event;
  EXPECT_CALL(write_event, ready()).Times(1);

  Event::FileEventPtr file_event = dispatcher_->createFileEvent(
      fds_[0],
      [&](uint32_t events) -> void {
        if (events & FileReadyType::Read) {
          read_event.ready();
        }

        if (events & FileReadyType::Write) {
          write_event.ready();
        }
      },
      FileTriggerType::Edge, FileReadyType::Read | FileReadyType::Write);

  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}
#endif

TEST_F(FileEventImplTest, LevelTrigger) {
  ReadyWatcher read_event;
  EXPECT_CALL(read_event, ready()).Times(2);
  ReadyWatcher write_event;
  EXPECT_CALL(write_event, ready()).Times(2);

  int count = 2;
  Event::FileEventPtr file_event = dispatcher_->createFileEvent(
      fds_[0],
      [&](uint32_t events) -> void {
        if (count-- == 0) {
          dispatcher_->exit();
          return;
        }
        if (events & FileReadyType::Read) {
          read_event.ready();
        }

        if (events & FileReadyType::Write) {
          write_event.ready();
        }
      },
      FileTriggerType::Level, FileReadyType::Read | FileReadyType::Write);

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_F(FileEventImplTest, SetEnabled) {
  ReadyWatcher read_event;
  EXPECT_CALL(read_event, ready()).Times(2);
  ReadyWatcher write_event;
  EXPECT_CALL(write_event, ready()).Times(2);

#ifdef WIN32
  const FileTriggerType trigger = FileTriggerType::Level;
#else
  const FileTriggerType trigger = FileTriggerType::Edge;
#endif
  Event::FileEventPtr file_event = dispatcher_->createFileEvent(
      fds_[0],
      [&](uint32_t events) -> void {
        if (events & FileReadyType::Read) {
          read_event.ready();
        }

        if (events & FileReadyType::Write) {
          write_event.ready();
        }
      },
      trigger, FileReadyType::Read | FileReadyType::Write);

  file_event->setEnabled(FileReadyType::Read);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  file_event->setEnabled(FileReadyType::Write);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  file_event->setEnabled(0);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  file_event->setEnabled(FileReadyType::Read | FileReadyType::Write);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

} // namespace
} // namespace Event
} // namespace Envoy
