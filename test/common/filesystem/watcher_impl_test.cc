#include "common/common/assert.h"
#include "common/event/dispatcher_impl.h"
#include "common/filesystem/watcher_impl.h"

namespace Filesystem {

class WatchCallback {
public:
  MOCK_METHOD1(called, void(uint32_t));
};

TEST(WatcherImplTest, All) {
  Event::DispatcherImpl dispatcher;
  Filesystem::WatcherPtr watcher = dispatcher.createFilesystemWatcher();

  unlink("/tmp/envoy_test/watcher_target");
  unlink("/tmp/envoy_test/watcher_link");
  unlink("/tmp/envoy_test/watcher_new_target");
  unlink("/tmp/envoy_test/watcher_new_link");

  mkdir("/tmp/envoy_test", S_IRWXU);
  { std::ofstream file("/tmp/envoy_test/watcher_target"); }
  int rc = symlink("/tmp/envoy_test/watcher_target", "/tmp/envoy_test/watcher_link");
  RELEASE_ASSERT(0 == rc);
  UNREFERENCED_PARAMETER(rc);

  { std::ofstream file("/tmp/envoy_test/watcher_new_target"); }
  rc = symlink("/tmp/envoy_test/watcher_new_target", "/tmp/envoy_test/watcher_new_link");
  RELEASE_ASSERT(0 == rc);

  WatchCallback callback;
  EXPECT_CALL(callback, called(Watcher::Events::MovedTo)).Times(2);
  watcher->addWatch("/tmp/envoy_test/watcher_link", Watcher::Events::MovedTo,
                    [&](uint32_t events) -> void {
                      callback.called(events);
                      dispatcher.exit();
                    });

  rename("/tmp/envoy_test/watcher_new_link", "/tmp/envoy_test/watcher_link");
  dispatcher.run(Event::Dispatcher::RunType::Block);

  rc = symlink("/tmp/envoy_test/watcher_new_target", "/tmp/envoy_test/watcher_new_link");
  RELEASE_ASSERT(0 == rc);
  rename("/tmp/envoy_test/watcher_new_link", "/tmp/envoy_test/watcher_link");
  dispatcher.run(Event::Dispatcher::RunType::Block);
}

} // Filesystem
