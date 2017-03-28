#include "common/common/assert.h"
#include "common/event/dispatcher_impl.h"
#include "common/filesystem/watcher_impl.h"

#include "test/test_common/environment.h"

namespace Filesystem {

class WatchCallback {
public:
  MOCK_METHOD1(called, void(uint32_t));
};

TEST(WatcherImplTest, All) {
  Event::DispatcherImpl dispatcher;
  Filesystem::WatcherPtr watcher = dispatcher.createFilesystemWatcher();

  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_target").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_link").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_target").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str());

  mkdir(TestEnvironment::temporaryPath("envoy_test").c_str(), S_IRWXU);
  { std::ofstream file(TestEnvironment::temporaryPath("envoy_test/watcher_target")); }
  int rc = symlink(TestEnvironment::temporaryPath("envoy_test/watcher_target").c_str(),
                   TestEnvironment::temporaryPath("envoy_test/watcher_link").c_str());
  RELEASE_ASSERT(0 == rc);
  UNREFERENCED_PARAMETER(rc);

  { std::ofstream file(TestEnvironment::temporaryPath("envoy_test/watcher_new_target")); }
  rc = symlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_target").c_str(),
               TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str());
  RELEASE_ASSERT(0 == rc);

  WatchCallback callback;
  EXPECT_CALL(callback, called(Watcher::Events::MovedTo)).Times(2);
  watcher->addWatch(TestEnvironment::temporaryPath("envoy_test/watcher_link"),
                    Watcher::Events::MovedTo, [&](uint32_t events) -> void {
                      callback.called(events);
                      dispatcher.exit();
                    });

  rename(TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str(),
         TestEnvironment::temporaryPath("envoy_test/watcher_link").c_str());
  dispatcher.run(Event::Dispatcher::RunType::Block);

  rc = symlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_target").c_str(),
               TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str());
  RELEASE_ASSERT(0 == rc);
  rename(TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str(),
         TestEnvironment::temporaryPath("envoy_test/watcher_link").c_str());
  dispatcher.run(Event::Dispatcher::RunType::Block);
}

} // Filesystem
