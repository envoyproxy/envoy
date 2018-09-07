#include <cstdint>

#include "envoy/event/file_event.h"

#include "common/event/dispatcher_impl.h"

#include "test/mocks/common.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Event {

class FileEventImplTest : public testing::Test {
public:
  FileEventImplTest() : dispatcher_(test_time_.timeSystem()) {}

  void SetUp() override {
    int rc = socketpair(AF_UNIX, SOCK_DGRAM, 0, fds_);
    ASSERT_EQ(0, rc);
    int data = 1;
    rc = write(fds_[1], &data, sizeof(data));
    ASSERT_EQ(sizeof(data), static_cast<size_t>(rc));
  }

  void TearDown() override {
    close(fds_[0]);
    close(fds_[1]);
  }

protected:
  int fds_[2];
  DangerousDeprecatedTestTime test_time_;
  DispatcherImpl dispatcher_;
};

class FileEventImplActivateTest : public testing::TestWithParam<Network::Address::IpVersion> {};

INSTANTIATE_TEST_CASE_P(IpVersions, FileEventImplActivateTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(FileEventImplActivateTest, Activate) {
  int fd;
  if (GetParam() == Network::Address::IpVersion::v4) {
    fd = socket(AF_INET, SOCK_STREAM, 0);
  } else {
    fd = socket(AF_INET6, SOCK_STREAM, 0);
  }
  ASSERT_NE(-1, fd);

  DangerousDeprecatedTestTime test_time;
  DispatcherImpl dispatcher(test_time.timeSystem());
  ReadyWatcher read_event;
  EXPECT_CALL(read_event, ready()).Times(1);
  ReadyWatcher write_event;
  EXPECT_CALL(write_event, ready()).Times(1);
  ReadyWatcher closed_event;
  EXPECT_CALL(closed_event, ready()).Times(1);

  Event::FileEventPtr file_event = dispatcher.createFileEvent(
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
      FileTriggerType::Edge, FileReadyType::Read | FileReadyType::Write | FileReadyType::Closed);

  file_event->activate(FileReadyType::Read | FileReadyType::Write | FileReadyType::Closed);
  dispatcher.run(Event::Dispatcher::RunType::NonBlock);

  close(fd);
}

TEST_F(FileEventImplTest, EdgeTrigger) {
  ReadyWatcher read_event;
  EXPECT_CALL(read_event, ready()).Times(1);
  ReadyWatcher write_event;
  EXPECT_CALL(write_event, ready()).Times(1);

  Event::FileEventPtr file_event = dispatcher_.createFileEvent(
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

  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
}

TEST_F(FileEventImplTest, LevelTrigger) {
  ReadyWatcher read_event;
  EXPECT_CALL(read_event, ready()).Times(2);
  ReadyWatcher write_event;
  EXPECT_CALL(write_event, ready()).Times(2);

  int count = 2;
  Event::FileEventPtr file_event = dispatcher_.createFileEvent(
      fds_[0],
      [&](uint32_t events) -> void {
        if (count-- == 0) {
          dispatcher_.exit();
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

  dispatcher_.run(Event::Dispatcher::RunType::Block);
}

TEST_F(FileEventImplTest, SetEnabled) {
  ReadyWatcher read_event;
  EXPECT_CALL(read_event, ready()).Times(2);
  ReadyWatcher write_event;
  EXPECT_CALL(write_event, ready()).Times(2);

  Event::FileEventPtr file_event = dispatcher_.createFileEvent(
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

  file_event->setEnabled(FileReadyType::Read);
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);

  file_event->setEnabled(FileReadyType::Write);
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);

  file_event->setEnabled(0);
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);

  file_event->setEnabled(FileReadyType::Read | FileReadyType::Write);
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
}

} // namespace Event
} // namespace Envoy
