#include <cstdint>
#include <fstream>

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/filesystem/watcher_impl.h"

#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Filesystem {

class WatcherImplTest : public testing::Test {
protected:
  WatcherImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
};

class WatchCallback {
public:
  MOCK_METHOD(void, called, (uint32_t));
};

TEST_F(WatcherImplTest, All) {
  Filesystem::WatcherPtr watcher = dispatcher_->createFilesystemWatcher();

  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_target").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_link").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_target").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str());

  TestEnvironment::createPath(TestEnvironment::temporaryPath("envoy_test"));
  { std::ofstream file(TestEnvironment::temporaryPath("envoy_test/watcher_target")); }
  TestEnvironment::createSymlink(TestEnvironment::temporaryPath("envoy_test/watcher_target"),
                                 TestEnvironment::temporaryPath("envoy_test/watcher_link"));

  { std::ofstream file(TestEnvironment::temporaryPath("envoy_test/watcher_new_target")); }
  TestEnvironment::createSymlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_target"),
                                 TestEnvironment::temporaryPath("envoy_test/watcher_new_link"));

  WatchCallback callback;
  EXPECT_CALL(callback, called(Watcher::Events::MovedTo)).Times(2);
  ASSERT_TRUE(watcher
                  ->addWatch(TestEnvironment::temporaryPath("envoy_test/watcher_link"),
                             Watcher::Events::MovedTo,
                             [&](uint32_t events) {
                               callback.called(events);
                               dispatcher_->exit();
                               return absl::OkStatus();
                             })
                  .ok());
  TestEnvironment::renameFile(TestEnvironment::temporaryPath("envoy_test/watcher_new_link"),
                              TestEnvironment::temporaryPath("envoy_test/watcher_link"));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  TestEnvironment::createSymlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_target"),
                                 TestEnvironment::temporaryPath("envoy_test/watcher_new_link"));
  TestEnvironment::renameFile(TestEnvironment::temporaryPath("envoy_test/watcher_new_link"),
                              TestEnvironment::temporaryPath("envoy_test/watcher_link"));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_F(WatcherImplTest, Create) {
  Filesystem::WatcherPtr watcher = dispatcher_->createFilesystemWatcher();

  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_target").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_link").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/other_file").c_str());

  TestEnvironment::createPath(TestEnvironment::temporaryPath("envoy_test"));
  { std::ofstream file(TestEnvironment::temporaryPath("envoy_test/watcher_target")); }

  WatchCallback callback;
  ASSERT_TRUE(watcher
                  ->addWatch(TestEnvironment::temporaryPath("envoy_test/watcher_link"),
                             Watcher::Events::MovedTo,
                             [&](uint32_t events) {
                               callback.called(events);
                               dispatcher_->exit();
                               return absl::OkStatus();
                             })
                  .ok());

  { std::ofstream file(TestEnvironment::temporaryPath("envoy_test/other_file")); }
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(callback, called(Watcher::Events::MovedTo));
  TestEnvironment::createSymlink(TestEnvironment::temporaryPath("envoy_test/watcher_target"),
                                 TestEnvironment::temporaryPath("envoy_test/watcher_new_link"));
  TestEnvironment::renameFile(TestEnvironment::temporaryPath("envoy_test/watcher_new_link"),
                              TestEnvironment::temporaryPath("envoy_test/watcher_link"));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_F(WatcherImplTest, Modify) {
  Filesystem::WatcherPtr watcher = dispatcher_->createFilesystemWatcher();

  TestEnvironment::createPath(TestEnvironment::temporaryPath("envoy_test"));
  std::ofstream file(TestEnvironment::temporaryPath("envoy_test/watcher_target"));

  WatchCallback callback;
  ASSERT_TRUE(watcher
                  ->addWatch(TestEnvironment::temporaryPath("envoy_test/watcher_target"),
                             Watcher::Events::Modified,
                             [&](uint32_t events) {
                               callback.called(events);
                               dispatcher_->exit();
                               return absl::OkStatus();
                             })
                  .ok());
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  file << "text" << std::flush;
  file.close();
  EXPECT_CALL(callback, called(Watcher::Events::Modified));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_F(WatcherImplTest, BadPath) {
  Filesystem::WatcherPtr watcher = dispatcher_->createFilesystemWatcher();

  EXPECT_FALSE(watcher
                   ->addWatch("this_is_not_a_file", Watcher::Events::MovedTo,
                              [&](uint32_t) { return absl::OkStatus(); })
                   .ok());

  EXPECT_FALSE(watcher
                   ->addWatch("this_is_not_a_dir/file", Watcher::Events::MovedTo,
                              [&](uint32_t) { return absl::OkStatus(); })
                   .ok());
}

TEST_F(WatcherImplTest, ParentDirectoryRemoved) {
  Filesystem::WatcherPtr watcher = dispatcher_->createFilesystemWatcher();

  TestEnvironment::createPath(TestEnvironment::temporaryPath("envoy_test_empty"));

  WatchCallback callback;
  EXPECT_CALL(callback, called(testing::_)).Times(0);

  ASSERT_TRUE(watcher
                  ->addWatch(TestEnvironment::temporaryPath("envoy_test_empty/watcher_link"),
                             Watcher::Events::MovedTo,
                             [&](uint32_t events) {
                               callback.called(events);
                               return absl::OkStatus();
                             })
                  .ok());

  int rc = rmdir(TestEnvironment::temporaryPath("envoy_test_empty").c_str());
  EXPECT_EQ(0, rc);

  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

TEST_F(WatcherImplTest, RootDirectoryPath) {
  Filesystem::WatcherPtr watcher = dispatcher_->createFilesystemWatcher();

#ifndef WIN32
  EXPECT_TRUE(
      watcher->addWatch("/", Watcher::Events::MovedTo, [&](uint32_t) { return absl::OkStatus(); })
          .ok());
#else
  EXPECT_TRUE(
      watcher
          ->addWatch("c:\\", Watcher::Events::MovedTo, [&](uint32_t) { return absl::OkStatus(); })
          .ok());
#endif
}

// Skipping this test on Windows as there is no Windows API able to atomically move a
// directory/symlink when the new name is a non-empty directory
#ifndef WIN32
TEST_F(WatcherImplTest, SymlinkAtomicRename) {
  Filesystem::WatcherPtr watcher = dispatcher_->createFilesystemWatcher();

  TestEnvironment::createPath(TestEnvironment::temporaryPath("envoy_test"));
  TestEnvironment::createPath(TestEnvironment::temporaryPath("envoy_test/..timestamp1"));
  { std::ofstream file(TestEnvironment::temporaryPath("envoy_test/..timestamp1/watched_file")); }

  TestEnvironment::createSymlink(TestEnvironment::temporaryPath("envoy_test/..timestamp1"),
                                 TestEnvironment::temporaryPath("envoy_test/..data"));
  TestEnvironment::createSymlink(TestEnvironment::temporaryPath("envoy_test/..data/watched_file"),
                                 TestEnvironment::temporaryPath("envoy_test/watched_file"));

  WatchCallback callback;
  EXPECT_CALL(callback, called(Watcher::Events::MovedTo));
  ASSERT_TRUE(watcher
                  ->addWatch(TestEnvironment::temporaryPath("envoy_test/"),
                             Watcher::Events::MovedTo,
                             [&](uint32_t events) {
                               callback.called(events);
                               dispatcher_->exit();
                               return absl::OkStatus();
                             })
                  .ok());

  TestEnvironment::createPath(TestEnvironment::temporaryPath("envoy_test/..timestamp2"));
  { std::ofstream file(TestEnvironment::temporaryPath("envoy_test/..timestamp2/watched_file")); }
  TestEnvironment::createSymlink(TestEnvironment::temporaryPath("envoy_test/..timestamp2"),
                                 TestEnvironment::temporaryPath("envoy_test/..tmp"));
  TestEnvironment::renameFile(TestEnvironment::temporaryPath("envoy_test/..tmp"),
                              TestEnvironment::temporaryPath("envoy_test/..data"));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}
#endif

// Test that callback returning error status is logged and doesn't crash.
TEST_F(WatcherImplTest, CallbackReturnsErrorStatus) {
  Filesystem::WatcherPtr watcher = dispatcher_->createFilesystemWatcher();

  TestEnvironment::createPath(TestEnvironment::temporaryPath("envoy_test"));
  std::ofstream file(TestEnvironment::temporaryPath("envoy_test/watcher_target"));

  WatchCallback callback;
  EXPECT_CALL(callback, called(Watcher::Events::Modified));
  ASSERT_TRUE(watcher
                  ->addWatch(TestEnvironment::temporaryPath("envoy_test/watcher_target"),
                             Watcher::Events::Modified,
                             [&](uint32_t events) {
                               callback.called(events);
                               dispatcher_->exit();
                               // Return an error status - should be logged but not crash.
                               return absl::InternalError("simulated callback error");
                             })
                  .ok());
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_LOG_CONTAINS("warn", "Filesystem watch callback for", file << "text" << std::flush;
                      file.close(); dispatcher_->run(Event::Dispatcher::RunType::Block););
}

// Test that callback throwing exception is caught and logged.
TEST_F(WatcherImplTest, CallbackThrowsException) {
  Filesystem::WatcherPtr watcher = dispatcher_->createFilesystemWatcher();

  TestEnvironment::createPath(TestEnvironment::temporaryPath("envoy_test"));
  std::ofstream file(TestEnvironment::temporaryPath("envoy_test/watcher_target"));

  WatchCallback callback;
  EXPECT_CALL(callback, called(Watcher::Events::Modified));
  ASSERT_TRUE(watcher
                  ->addWatch(TestEnvironment::temporaryPath("envoy_test/watcher_target"),
                             Watcher::Events::Modified,
                             [&](uint32_t events) -> absl::Status {
                               callback.called(events);
                               dispatcher_->exit();
                               // Throw an exception - should be caught and logged.
                               throw EnvoyException("simulated callback exception");
                             })
                  .ok());
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_LOG_CONTAINS("warn", "threw exception", file << "text" << std::flush; file.close();
                      dispatcher_->run(Event::Dispatcher::RunType::Block););
}

// Test that multiple callbacks can fail without affecting each other.
TEST_F(WatcherImplTest, MultipleCallbacksWithErrors) {
  Filesystem::WatcherPtr watcher = dispatcher_->createFilesystemWatcher();

  TestEnvironment::createPath(TestEnvironment::temporaryPath("envoy_test"));
  std::ofstream file(TestEnvironment::temporaryPath("envoy_test/watcher_target"));

  int callback_count = 0;
  ASSERT_TRUE(watcher
                  ->addWatch(TestEnvironment::temporaryPath("envoy_test/watcher_target"),
                             Watcher::Events::Modified,
                             [&](uint32_t) {
                               callback_count++;
                               if (callback_count >= 2) {
                                 dispatcher_->exit();
                               }
                               // First callback returns error, second returns OK.
                               if (callback_count == 1) {
                                 return absl::InternalError("first callback error");
                               }
                               return absl::OkStatus();
                             })
                  .ok());
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Trigger first modification. The first callback returns error, but watcher continues.
  file << "text1" << std::flush;
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Trigger second modification. It should still work.
  file << "text2" << std::flush;
  file.close();
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(2, callback_count);
}

} // namespace Filesystem
} // namespace Envoy
