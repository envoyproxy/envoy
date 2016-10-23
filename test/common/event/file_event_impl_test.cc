#include "envoy/event/file_event.h"

#include "common/event/dispatcher_impl.h"

#include "test/mocks/common.h"

namespace Event {

TEST(FileEventImplTest, All) {
  int fds[2];
  int rc = socketpair(AF_UNIX, SOCK_DGRAM, 0, fds);
  ASSERT_EQ(0, rc);

  DispatcherImpl dispatcher;
  ReadyWatcher read_event;
  EXPECT_CALL(read_event, ready());
  ReadyWatcher write_event;
  EXPECT_CALL(write_event, ready());

  int data = 1;
  rc = write(fds[1], &data, sizeof(data));
  ASSERT_EQ(sizeof(data), static_cast<size_t>(rc));
  Event::FileEventPtr file_event = dispatcher.createFileEvent(fds[0], [&](uint32_t events) -> void {
    if (events & FileReadyType::Read) {
      read_event.ready();
    }

    if (events & FileReadyType::Write) {
      write_event.ready();
      dispatcher.exit();
    }
  });

  dispatcher.run(Event::Dispatcher::RunType::Block);
  close(fds[0]);
  close(fds[1]);
}

} // Event
