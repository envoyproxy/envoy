#include "envoy/filesystem/watcher.h"

#include "source/common/config/watched_directory.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/test_common/test_runtime.h"

#include "gtest/gtest.h"

using testing::DoAll;
using testing::Return;
using testing::SaveArg;

namespace Envoy {
namespace Config {

TEST(WatchedDirectory, All) {
  Event::MockDispatcher dispatcher;
  envoy::config::core::v3::WatchedDirectory config;
  config.set_path("foo/bar");
  auto* watcher = new Filesystem::MockWatcher();
  EXPECT_CALL(dispatcher, createFilesystemWatcher_()).WillOnce(Return(watcher));
  Filesystem::Watcher::OnChangedCb cb;
  EXPECT_CALL(*watcher, addWatch("foo/bar/", Filesystem::Watcher::Events::MovedTo, _))
      .WillOnce(DoAll(SaveArg<2>(&cb), Return(absl::OkStatus())));
  auto wd = *WatchedDirectory::create(config, dispatcher);
  bool called = false;
  wd->setCallback([&called] {
    called = true;
    return absl::OkStatus();
  });
  EXPECT_TRUE(cb(Filesystem::Watcher::Events::MovedTo).ok());
  EXPECT_TRUE(called);
}

// Verify that watch callback doesn't crash if setCallback() was never called.
TEST(WatchedDirectory, CallbackNotSetDoesNotCrash) {
  Event::MockDispatcher dispatcher;
  envoy::config::core::v3::WatchedDirectory config;
  config.set_path("foo/bar");
  auto* watcher = new Filesystem::MockWatcher();
  EXPECT_CALL(dispatcher, createFilesystemWatcher_()).WillOnce(Return(watcher));
  Filesystem::Watcher::OnChangedCb cb;
  EXPECT_CALL(*watcher, addWatch("foo/bar/", Filesystem::Watcher::Events::MovedTo, _))
      .WillOnce(DoAll(SaveArg<2>(&cb), Return(absl::OkStatus())));
  auto wd = *WatchedDirectory::create(config, dispatcher);
  EXPECT_TRUE(cb(Filesystem::Watcher::Events::MovedTo).ok());
}

// Verify that with the runtime guard enabled, WatchedDirectory subscribes to both
// MovedTo and Modified events, so in-place file writes trigger the callback.
TEST(WatchedDirectory, ModifiedEventWithRuntimeGuardEnabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.watched_directory_modified_events", "true"}});

  Event::MockDispatcher dispatcher;
  envoy::config::core::v3::WatchedDirectory config;
  config.set_path("foo/bar");
  auto* watcher = new Filesystem::MockWatcher();
  EXPECT_CALL(dispatcher, createFilesystemWatcher_()).WillOnce(Return(watcher));
  Filesystem::Watcher::OnChangedCb cb;
  EXPECT_CALL(*watcher,
              addWatch("foo/bar/",
                       Filesystem::Watcher::Events::MovedTo | Filesystem::Watcher::Events::Modified,
                       _))
      .WillOnce(DoAll(SaveArg<2>(&cb), Return(absl::OkStatus())));
  auto wd = *WatchedDirectory::create(config, dispatcher);
  bool called = false;
  wd->setCallback([&called] {
    called = true;
    return absl::OkStatus();
  });
  EXPECT_TRUE(cb(Filesystem::Watcher::Events::Modified).ok());
  EXPECT_TRUE(called);
}

} // namespace Config
} // namespace Envoy
