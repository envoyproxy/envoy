#include <cstdint>
#include <fstream>

#include "common/common/assert.h"
#include "common/event/dispatcher_impl.h"
#include "common/filesystem/watcher_impl.h"

#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
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
  EXPECT_EQ(0, rc);

  { std::ofstream file(TestEnvironment::temporaryPath("envoy_test/watcher_new_target")); }
  rc = symlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_target").c_str(),
               TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str());
  EXPECT_EQ(0, rc);

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
  EXPECT_EQ(0, rc);
  rename(TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str(),
         TestEnvironment::temporaryPath("envoy_test/watcher_link").c_str());
  dispatcher.run(Event::Dispatcher::RunType::Block);
}

TEST(WatcherImplTest, Create) {
  Event::DispatcherImpl dispatcher;
  Filesystem::WatcherPtr watcher = dispatcher.createFilesystemWatcher();

  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_target").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_link").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/other_file").c_str());

  mkdir(TestEnvironment::temporaryPath("envoy_test").c_str(), S_IRWXU);
  { std::ofstream file(TestEnvironment::temporaryPath("envoy_test/watcher_target")); }

  WatchCallback callback;
  EXPECT_CALL(callback, called(Watcher::Events::MovedTo));
  watcher->addWatch(TestEnvironment::temporaryPath("envoy_test/watcher_link"),
                    Watcher::Events::MovedTo, [&](uint32_t events) -> void {
                      callback.called(events);
                      dispatcher.exit();
                    });

  { std::ofstream file(TestEnvironment::temporaryPath("envoy_test/other_file")); }
  dispatcher.run(Event::Dispatcher::RunType::NonBlock);

  int rc = symlink(TestEnvironment::temporaryPath("envoy_test/watcher_target").c_str(),
                   TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str());
  EXPECT_EQ(0, rc);

  rc = rename(TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str(),
              TestEnvironment::temporaryPath("envoy_test/watcher_link").c_str());
  EXPECT_EQ(0, rc);

  dispatcher.run(Event::Dispatcher::RunType::Block);
}

TEST(WatcherImplTest, BadPath) {
  Event::DispatcherImpl dispatcher;
  Filesystem::WatcherPtr watcher = dispatcher.createFilesystemWatcher();

  EXPECT_THROW(
      watcher->addWatch("this_is_not_a_file", Watcher::Events::MovedTo, [&](uint32_t) -> void {}),
      EnvoyException);

  EXPECT_THROW(watcher->addWatch("this_is_not_a_dir/file", Watcher::Events::MovedTo,
                                 [&](uint32_t) -> void {}),
               EnvoyException);
}

TEST(WatcherImplTest, ParentDirectoryRemoved) {
  Event::DispatcherImpl dispatcher;
  Filesystem::WatcherPtr watcher = dispatcher.createFilesystemWatcher();

  mkdir(TestEnvironment::temporaryPath("envoy_test_empty").c_str(), S_IRWXU);

  WatchCallback callback;
  EXPECT_CALL(callback, called(testing::_)).Times(0);

  watcher->addWatch(TestEnvironment::temporaryPath("envoy_test_empty/watcher_link"),
                    Watcher::Events::MovedTo,
                    [&](uint32_t events) -> void { callback.called(events); });

  int rc = rmdir(TestEnvironment::temporaryPath("envoy_test_empty").c_str());
  EXPECT_EQ(0, rc);

  dispatcher.run(Event::Dispatcher::RunType::NonBlock);
}

} // namespace Filesystem
} // namespace Envoy
