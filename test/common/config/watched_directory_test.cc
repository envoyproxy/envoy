#include "envoy/filesystem/watcher.h"

#include "source/common/config/watched_directory.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"

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
  WatchedDirectory wd(config, dispatcher);
  bool called = false;
  wd.setCallback([&called] {
    called = true;
    return absl::OkStatus();
  });
  EXPECT_TRUE(cb(Filesystem::Watcher::Events::MovedTo).ok());
  EXPECT_TRUE(called);
}

} // namespace Config
} // namespace Envoy
