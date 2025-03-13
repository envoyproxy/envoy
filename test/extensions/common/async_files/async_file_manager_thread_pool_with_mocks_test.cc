#include <cerrno>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include "envoy/extensions/common/async_files/v3/async_file_manager.pb.h"

#include "source/extensions/common/async_files/async_file_action.h"
#include "source/extensions/common/async_files/async_file_handle.h"
#include "source/extensions/common/async_files/async_file_manager.h"
#include "source/extensions/common/async_files/async_file_manager_factory.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

using StatusHelpers::IsOkAndHolds;
using ::testing::_;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::StrictMock;

class AsyncFileManagerWithMockFilesTest : public ::testing::Test {
public:
  void SetUp() override {
    EXPECT_CALL(mock_posix_file_operations_, supportsAllPosixFileOperations())
        .WillRepeatedly(Return(true));
    envoy::extensions::common::async_files::v3::AsyncFileManagerConfig config;
    config.mutable_thread_pool()->set_thread_count(1);
    singleton_manager_ = std::make_unique<Singleton::ManagerImpl>();
    factory_ = AsyncFileManagerFactory::singleton(singleton_manager_.get());
    manager_ = factory_->getAsyncFileManager(config, &mock_posix_file_operations_);
  }

  void resolveFileActions(int cycles = 1) {
    for (int i = 0; i < cycles; i++) {
      manager_->waitForIdle();
      dispatcher_->run(Event::Dispatcher::RunType::Block);
    }
  }

protected:
  std::unique_ptr<Singleton::ManagerImpl> singleton_manager_;
  StrictMock<Api::MockOsSysCalls> mock_posix_file_operations_;
  std::shared_ptr<AsyncFileManagerFactory> factory_;
  std::shared_ptr<AsyncFileManager> manager_;
  static constexpr absl::string_view tmpdir_{"/mocktmp"};
  Api::ApiPtr api_ = Api::createApiForTest();
  Event::DispatcherPtr dispatcher_ = api_->allocateDispatcher("test_thread");
};

TEST_F(AsyncFileManagerWithMockFilesTest, CancellingAQueuedActionPreventsItFromExecuting) {
  std::promise<void> ready;
  EXPECT_CALL(mock_posix_file_operations_, stat(_, _)).WillOnce([&](const char*, struct stat*) {
    ready.get_future().wait();
    return Api::SysCallIntResult{0, 0};
  });
  // Queue a blocking action so we can guarantee cancel is called before unlink begins.
  manager_->stat(dispatcher_.get(), tmpdir_, [&](absl::StatusOr<struct stat>) {});
  // Ensure that unlink doesn't get called.
  EXPECT_CALL(mock_posix_file_operations_, unlink(_)).Times(0);
  auto cancel_unlink = manager_->unlink(dispatcher_.get(), "irrelevant", [](absl::Status) {
    FAIL() << "canceled action should not call callback";
  });
  cancel_unlink();
  ready.set_value();
  resolveFileActions();
}

TEST_F(AsyncFileManagerWithMockFilesTest, CancellingACompletedActionDoesNothingImportant) {
  EXPECT_CALL(mock_posix_file_operations_, stat(_, _));
  auto cancel =
      manager_->stat(dispatcher_.get(), "irrelevant", [&](absl::StatusOr<struct stat>) {});
  resolveFileActions();
  // This is to make sure cancel() doesn't end up with a dangling pointer or something
  // when the action is resolved.
  cancel();
}

TEST_F(AsyncFileManagerWithMockFilesTest,
       CancellingDuringCreateAnonymousFileClosesFileAndPreventsCallback) {
  int fd = 1;
  std::promise<void> wait_for_open_to_be_executing;
  std::promise<void> allow_open_to_finish;
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce([&](const char*, int, int) {
        wait_for_open_to_be_executing.set_value();
        allow_open_to_finish.get_future().wait();
        return Api::SysCallIntResult{fd, 0};
      });
  EXPECT_CALL(mock_posix_file_operations_, stat(_, _));
  auto cancelOpen = manager_->createAnonymousFile(
      dispatcher_.get(), tmpdir_,
      [&](absl::StatusOr<AsyncFileHandle>) { FAIL() << "callback should not be called"; });
  // Separately queue another action.
  bool did_second_action = false;
  manager_->stat(dispatcher_.get(), "",
                 [&](absl::StatusOr<struct stat>) { did_second_action = true; });
  // Wait for the open operation to be entered.
  wait_for_open_to_be_executing.get_future().wait();
  // Cancel the open request (but too late to actually stop it!)
  cancelOpen();
  // Expect the automatic close operation to occur.
  EXPECT_CALL(mock_posix_file_operations_, close(fd)).WillOnce(Return(Api::SysCallIntResult{0, 0}));
  // Allow the open operation to complete.
  allow_open_to_finish.set_value();
  resolveFileActions();
  // Ensure that the second action does get a turn after the file operation relinquishes the thread.
  // (This also ensures that the file operation reached the end, so the file should be closed now.)
  EXPECT_TRUE(did_second_action);
}

TEST_F(AsyncFileManagerWithMockFilesTest,
       CancellingDuringOpenExistingFileClosesFileAndPreventsCallback) {
  int fd = 1;
  std::string filename = absl::StrCat(tmpdir_, "/fake_file");
  std::promise<void> wait_for_open_to_be_executing;
  std::promise<void> allow_open_to_finish;
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(filename), O_RDWR)).WillOnce([&]() {
    wait_for_open_to_be_executing.set_value();
    allow_open_to_finish.get_future().wait();
    return Api::SysCallIntResult{fd, 0};
  });
  EXPECT_CALL(mock_posix_file_operations_, stat(_, _));
  auto cancelOpen = manager_->openExistingFile(
      dispatcher_.get(), filename, AsyncFileManager::Mode::ReadWrite,
      [&](absl::StatusOr<AsyncFileHandle>) { FAIL() << "callback should not be called"; });
  // Separately queue another action.
  bool did_second_action;
  manager_->stat(dispatcher_.get(), "irrelevant",
                 [&](absl::StatusOr<struct stat>) { did_second_action = true; });
  // Wait for the open operation to be entered.
  wait_for_open_to_be_executing.get_future().wait();
  // Cancel the open request (but too late to actually stop it!)
  cancelOpen();
  // Expect the automatic close operation to occur.
  EXPECT_CALL(mock_posix_file_operations_, close(fd)).WillOnce(Return(Api::SysCallIntResult{0, 0}));
  // Allow the open operation to complete.
  allow_open_to_finish.set_value();
  resolveFileActions();
  // Ensure that the second action does get a turn after the file operation relinquishes the thread.
  // (This also ensures that the file operation reached the end, so the file should be closed now.)
  EXPECT_TRUE(did_second_action);
}

TEST_F(AsyncFileManagerWithMockFilesTest, CloseActionExecutesEvenIfCancelled) {
  int fd = 1;
  // First do a successful open so we have a file handle.
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce(Return(Api::SysCallIntResult{fd, 0}));
  AsyncFileHandle handle;
  manager_->createAnonymousFile(
      dispatcher_.get(), tmpdir_,
      [&](absl::StatusOr<AsyncFileHandle> result) { handle = std::move(result.value()); });
  resolveFileActions();
  ASSERT_THAT(handle, testing::NotNull());
  EXPECT_CALL(mock_posix_file_operations_, close(fd));
  CancelFunction cancel = handle->close(nullptr, [](absl::Status) {}).value();
  cancel();
  resolveFileActions();
}

TEST_F(AsyncFileManagerWithMockFilesTest, CancellingBeforeCallbackUndoesActionsWithSideEffects) {
  int fd = 1;
  // First do a successful open so we have a file handle.
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce(Return(Api::SysCallIntResult{fd, 0}));
  AsyncFileHandle handle;
  manager_->createAnonymousFile(
      dispatcher_.get(), tmpdir_,
      [&](absl::StatusOr<AsyncFileHandle> result) { handle = std::move(result.value()); });
  resolveFileActions();
  ASSERT_THAT(handle, testing::NotNull());
  EXPECT_CALL(mock_posix_file_operations_, linkat(fd, _, _, Eq(tmpdir_), _));
  CancelFunction cancel_hard_link =
      handle
          ->createHardLink(
              dispatcher_.get(), tmpdir_,
              [&](absl::Status) { FAIL() << "callback should not be called in this test"; })
          .value();
  // wait for the manager to post to the dispatcher, but don't consume it yet.
  manager_->waitForIdle();
  // cancel while it's in the dispatcher queue.
  cancel_hard_link();
  // Cancellation should remove the link because the callback was not executed.
  EXPECT_CALL(mock_posix_file_operations_, unlink(Eq(tmpdir_)));
  resolveFileActions();
  EXPECT_CALL(mock_posix_file_operations_, close(fd)).WillOnce(Return(Api::SysCallIntResult{0, 0}));
  ASSERT_OK(handle->close(nullptr, [](absl::Status) {}));
  resolveFileActions();
}

TEST_F(AsyncFileManagerWithMockFilesTest, OpenFailureInCreateAnonymousReturnsAnError) {
  int fd = 1;
  // First do a successful open and close, to establish that we can use the O_TMPFILE path
  // (otherwise a failure will retry using 'mkstemp').
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce(Return(Api::SysCallIntResult{fd, 0}));
  EXPECT_CALL(mock_posix_file_operations_, close(fd)).WillOnce(Return(Api::SysCallIntResult{0, 0}));
  AsyncFileHandle handle;
  manager_->createAnonymousFile(
      dispatcher_.get(), tmpdir_,
      [&](absl::StatusOr<AsyncFileHandle> result) { handle = std::move(result.value()); });
  resolveFileActions();
  ASSERT_THAT(handle, testing::NotNull());
  EXPECT_OK(handle->close(nullptr, [](absl::Status) {}));
  resolveFileActions();
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce(Return(Api::SysCallIntResult{-1, EMFILE}));
  // Capture the result of the second open call, to verify that the error code came through.
  absl::Status captured_result;
  manager_->createAnonymousFile(
      dispatcher_.get(), tmpdir_,
      [&](absl::StatusOr<AsyncFileHandle> result) { captured_result = result.status(); });
  resolveFileActions();
  EXPECT_EQ(absl::StatusCode::kResourceExhausted, captured_result.code()) << captured_result;
}

TEST_F(AsyncFileManagerWithMockFilesTest, CreateAnonymousFallbackMkstempReturnsAnErrorOnFailure) {
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce(Return(Api::SysCallIntResult{-1, EBADF}));
  EXPECT_CALL(mock_posix_file_operations_, mkstemp(Eq(std::string(tmpdir_) + "/buffer.XXXXXX")))
      .WillOnce(Return(Api::SysCallIntResult{-1, EMFILE}));
  // Capture the result of the open call, to verify that the error code came through.
  absl::Status captured_result;
  manager_->createAnonymousFile(
      dispatcher_.get(), tmpdir_,
      [&](absl::StatusOr<AsyncFileHandle> result) { captured_result = result.status(); });
  resolveFileActions();
  EXPECT_EQ(absl::StatusCode::kResourceExhausted, captured_result.code()) << captured_result;
}

TEST_F(AsyncFileManagerWithMockFilesTest, CreateAnonymousFallbackReturnsAnErrorIfPathTooLong) {
  std::string too_long_tmpdir{"/a_long_path"};
  while (too_long_tmpdir.size() < 4096) {
    too_long_tmpdir += too_long_tmpdir;
  }
  EXPECT_CALL(mock_posix_file_operations_,
              open(Eq(too_long_tmpdir), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce(Return(Api::SysCallIntResult{-1, EBADF}));
  // Capture the result of the open call, to verify that the error code came through.
  absl::Status captured_result;
  manager_->createAnonymousFile(
      dispatcher_.get(), too_long_tmpdir,
      [&](absl::StatusOr<AsyncFileHandle> result) { captured_result = result.status(); });
  resolveFileActions();
  EXPECT_EQ(absl::StatusCode::kInvalidArgument, captured_result.code()) << captured_result;
}

TEST_F(AsyncFileManagerWithMockFilesTest,
       CreateAnonymousFallbackClosesAndReturnsAnErrorIfUnlinkWhileOpenFails) {
  int fd = 1;
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce(Return(Api::SysCallIntResult{-1, EBADF}));
  EXPECT_CALL(mock_posix_file_operations_, mkstemp(Eq(absl::StrCat(tmpdir_, "/buffer.XXXXXX"))))
      .WillOnce([&fd](char* tmplate) {
        memcpy(tmplate + strlen(tmplate) - 6, "ABCDEF", 6);
        return Api::SysCallIntResult{fd, 0};
      });
  // First unlink fails while the file is open, second unlink after close succeeds.
  {
    InSequence s;
    EXPECT_CALL(mock_posix_file_operations_, unlink(Eq(absl::StrCat(tmpdir_, "/buffer.ABCDEF"))))
        .WillOnce(Return(Api::SysCallIntResult{-1, EBADF}));
    EXPECT_CALL(mock_posix_file_operations_, close(fd))
        .WillOnce(Return(Api::SysCallIntResult{0, 0}));
    EXPECT_CALL(mock_posix_file_operations_, unlink(Eq(absl::StrCat(tmpdir_, "/buffer.ABCDEF"))))
        .WillOnce(Return(Api::SysCallIntResult{0, 0}));
  }
  absl::Status captured_result;
  manager_->createAnonymousFile(
      dispatcher_.get(), tmpdir_,
      [&](absl::StatusOr<AsyncFileHandle> result) { captured_result = result.status(); });
  resolveFileActions();
  EXPECT_EQ(absl::StatusCode::kUnimplemented, captured_result.code()) << captured_result;
}

TEST_F(AsyncFileManagerWithMockFilesTest,
       CreateAnonymousFallbackToMkstempWorksAsIntendedIfNoErrorsOccur) {
  int fd = 1;
  {
    InSequence s;
    EXPECT_CALL(mock_posix_file_operations_,
                open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
        .WillOnce(Return(Api::SysCallIntResult{-1, EBADF}));
    EXPECT_CALL(mock_posix_file_operations_, mkstemp(Eq(absl::StrCat(tmpdir_, "/buffer.XXXXXX"))))
        .WillOnce([&fd](char* tmplate) {
          memcpy(tmplate + strlen(tmplate) - 6, "ABCDEF", 6);
          return Api::SysCallIntResult{fd, 0};
        });
    EXPECT_CALL(mock_posix_file_operations_, unlink(Eq(absl::StrCat(tmpdir_, "/buffer.ABCDEF"))))
        .WillOnce(Return(Api::SysCallIntResult{0, 0}));
    EXPECT_CALL(mock_posix_file_operations_, close(fd))
        .WillOnce(Return(Api::SysCallIntResult{0, 0}));
  }
  bool callbacks_complete = false;
  manager_->createAnonymousFile(
      dispatcher_.get(), tmpdir_, [&](absl::StatusOr<AsyncFileHandle> result) {
        EXPECT_OK(result);
        EXPECT_OK(result.value()->close(dispatcher_.get(), [&](absl::Status result) {
          EXPECT_OK(result);
          callbacks_complete = true;
        }));
      });
  resolveFileActions(2);
  EXPECT_TRUE(callbacks_complete);
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
