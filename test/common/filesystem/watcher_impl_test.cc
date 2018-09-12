#include <cstdint>
#include <fstream>

#include "common/common/assert.h"
#include "common/event/dispatcher_impl.h"
#include "common/filesystem/watcher_impl.h"

#include "test/test_common/environment.h"
#include "test/test_common/test_time.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Filesystem {

class WatcherImplTest : public testing::Test {
protected:
  WatcherImplTest() : dispatcher_(test_time_.timeSystem()) {}

  DangerousDeprecatedTestTime test_time_;
  Event::DispatcherImpl dispatcher_;
};

class WatchCallback {
public:
  MOCK_METHOD1(called, void(uint32_t));
};

TEST_F(WatcherImplTest, All) {
  Filesystem::WatcherPtr watcher = dispatcher_.createFilesystemWatcher();

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
                      dispatcher_.exit();
                    });

  rename(TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str(),
         TestEnvironment::temporaryPath("envoy_test/watcher_link").c_str());
  dispatcher_.run(Event::Dispatcher::RunType::Block);

  rc = symlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_target").c_str(),
               TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str());
  EXPECT_EQ(0, rc);
  rename(TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str(),
         TestEnvironment::temporaryPath("envoy_test/watcher_link").c_str());
  dispatcher_.run(Event::Dispatcher::RunType::Block);
}

TEST_F(WatcherImplTest, Create) {
  Filesystem::WatcherPtr watcher = dispatcher_.createFilesystemWatcher();

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
                      dispatcher_.exit();
                    });

  { std::ofstream file(TestEnvironment::temporaryPath("envoy_test/other_file")); }
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);

  int rc = symlink(TestEnvironment::temporaryPath("envoy_test/watcher_target").c_str(),
                   TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str());
  EXPECT_EQ(0, rc);

  rc = rename(TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str(),
              TestEnvironment::temporaryPath("envoy_test/watcher_link").c_str());
  EXPECT_EQ(0, rc);

  dispatcher_.run(Event::Dispatcher::RunType::Block);
}

TEST_F(WatcherImplTest, BadPath) {
  Filesystem::WatcherPtr watcher = dispatcher_.createFilesystemWatcher();

  EXPECT_THROW(
      watcher->addWatch("this_is_not_a_file", Watcher::Events::MovedTo, [&](uint32_t) -> void {}),
      EnvoyException);

  EXPECT_THROW(watcher->addWatch("this_is_not_a_dir/file", Watcher::Events::MovedTo,
                                 [&](uint32_t) -> void {}),
               EnvoyException);
}

TEST_F(WatcherImplTest, ParentDirectoryRemoved) {
  Filesystem::WatcherPtr watcher = dispatcher_.createFilesystemWatcher();

  mkdir(TestEnvironment::temporaryPath("envoy_test_empty").c_str(), S_IRWXU);

  WatchCallback callback;
  EXPECT_CALL(callback, called(testing::_)).Times(0);

  watcher->addWatch(TestEnvironment::temporaryPath("envoy_test_empty/watcher_link"),
                    Watcher::Events::MovedTo,
                    [&](uint32_t events) -> void { callback.called(events); });

  int rc = rmdir(TestEnvironment::temporaryPath("envoy_test_empty").c_str());
  EXPECT_EQ(0, rc);

  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
}

TEST_F(WatcherImplTest, RootDirectoryPath) {
  Filesystem::WatcherPtr watcher = dispatcher_.createFilesystemWatcher();

  EXPECT_NO_THROW(watcher->addWatch("/", Watcher::Events::MovedTo, [&](uint32_t) -> void {}));
}

} // namespace Filesystem
} // namespace Envoy
