#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/common/async_files/async_file_action.h"
#include "source/extensions/common/async_files/async_file_context_basic.h"
#include "source/extensions/common/async_files/async_file_context_impl.h"
#include "source/extensions/common/async_files/async_file_handle.h"
#include "source/extensions/common/async_files/async_file_manager.h"

#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

int testOnlyGetFileDescriptor(AsyncFileContext* context);

class MockAsyncFileManager : public AsyncFileManager {
public:
  MOCK_METHOD(AsyncFileHandle, newFileHandle, ());
  MOCK_METHOD(void, enqueue, (const std::shared_ptr<AsyncFileContextImpl> context));
  MOCK_METHOD(unsigned int, threadPoolSize, (), (const));
};

class AsyncFileActionBlockedUntilReleased : public AsyncFileAction {
public:
  explicit AsyncFileActionBlockedUntilReleased(bool* completed) : completed_(completed) {}
  void execute(AsyncFileContextImpl*) override {
    absl::MutexLock lock(&blocking_mutex_);
    ASSERT_EQ(0, stage_);
    stage_ = 1;
    auto condition = [this]()
                         ABSL_EXCLUSIVE_LOCKS_REQUIRED(blocking_mutex_) { return stage_ == 2; };
    blocking_mutex_.Await(absl::Condition(&condition));
    stage_ = 3;
  }
  void callback() override {
    absl::MutexLock lock(&blocking_mutex_);
    stage_ = 4;
    auto condition = [this]()
                         ABSL_EXCLUSIVE_LOCKS_REQUIRED(blocking_mutex_) { return stage_ == 5; };
    blocking_mutex_.Await(absl::Condition(&condition));
    *completed_ = true;
  }
  absl::Mutex blocking_mutex_;
  // 0 for unstarted, 1 for blocking, 2 for unblocked, 3 for done.
  int stage_ GUARDED_BY(blocking_mutex_) = 0;
  bool* completed_;
  void waitUntilExecutionBlocked() ABSL_LOCKS_EXCLUDED(blocking_mutex_) {
    absl::MutexLock lock(&blocking_mutex_);
    auto condition = [this]()
                         ABSL_EXCLUSIVE_LOCKS_REQUIRED(blocking_mutex_) { return stage_ == 1; };
    blocking_mutex_.Await(absl::Condition(&condition));
  }
  void waitUntilCallbackBlocked() ABSL_LOCKS_EXCLUDED(blocking_mutex_) {
    absl::MutexLock lock(&blocking_mutex_);
    auto condition = [this]()
                         ABSL_EXCLUSIVE_LOCKS_REQUIRED(blocking_mutex_) { return stage_ == 4; };
    blocking_mutex_.Await(absl::Condition(&condition));
  }
  void unblockExecution() ABSL_LOCKS_EXCLUDED(blocking_mutex_) {
    absl::MutexLock lock(&blocking_mutex_);
    ASSERT_EQ(1, stage_);
    stage_ = 2;
  }
  void unblockCallback() ABSL_LOCKS_EXCLUDED(blocking_mutex_) {
    absl::MutexLock lock(&blocking_mutex_);
    ASSERT_EQ(4, stage_);
    stage_ = 5;
  }
};

class AsyncFileHandleTest : public testing::Test {
public:
  void SetUp() override {
    ON_CALL(mockAsyncFileManager_, enqueue)
        .WillByDefault([this](const std::shared_ptr<AsyncFileContextImpl> context) {
          queue_.push_back(std::move(context));
          total_actions_queued_++;
        });
    EXPECT_CALL(mockAsyncFileManager_, enqueue).Times(testing::AtLeast(0));
  }
  std::shared_ptr<AsyncFileContextImpl> popQueue() {
    auto item = std::move(queue_.front());
    queue_.erase(queue_.begin());
    return item;
  }
  void resolveContext(std::shared_ptr<AsyncFileContextImpl> context) { context->resolve(); }
  void advanceQueue() { resolveContext(popQueue()); }
  void createAnonymousFile() {
    absl::Status createStatus = absl::NotFoundError("not called yet");
    handle_->createAnonymousFile(tmpdir_,
                                 [&createStatus](absl::Status status) { createStatus = status; });
    ASSERT_EQ(1U, queue_.size());
    advanceQueue();
    ASSERT_EQ(0U, queue_.size());
    EXPECT_EQ(absl::OkStatus(), createStatus);
  }
  absl::Status openExistingFile(std::string name, AsyncFileContext::Mode mode) {
    absl::Status openStatus = absl::NotFoundError("not called yet");
    handle_->openExistingFile(std::move(name), mode,
                              [&openStatus](absl::Status status) { openStatus = status; });
    advanceQueue();
    return openStatus;
  }
  int& fileDescriptor() {
    return std::static_pointer_cast<AsyncFileContextBasic>(handle_)->file_descriptor_;
  }
  void forciblyCloseFileDescriptor() { ::close(fileDescriptor()); }
  bool fileWasClosed() { return fileDescriptor() == -1; }
  void close() {
    absl::Status closeStatus = absl::NotFoundError("not called yet");
    handle_->close([&closeStatus](absl::Status status) { closeStatus = status; });
    advanceQueue();
    EXPECT_EQ(absl::OkStatus(), closeStatus);
  }
  void waitForAborting() {
    auto impl = dynamic_cast<AsyncFileContextImpl*>(handle_.get());
    {
      absl::MutexLock lock(&impl->action_mutex_);
      auto condition = [impl]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(impl->action_mutex_) {
        return impl->aborting_;
      };
      impl->action_mutex_.Await(absl::Condition(&condition));
    }
  }
  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  std::string tmpdir_ = test_tmpdir ? test_tmpdir : "/tmp";
  MockAsyncFileManager mockAsyncFileManager_;
  std::vector<std::shared_ptr<AsyncFileContextImpl>> queue_;
  std::shared_ptr<AsyncFileContextImpl> handle_ =
      AsyncFileContextBasic::create(&mockAsyncFileManager_);
  unsigned int total_actions_queued_ = 0;
  AsyncFileActionBlockedUntilReleased* blocker_;
  bool blocker_callback_was_called_ = false;
  void enqueueBlocker() {
    auto blocker =
        std::make_unique<AsyncFileActionBlockedUntilReleased>(&blocker_callback_was_called_);
    blocker_ = blocker.get();
    handle_->enqueue(std::move(blocker));
  }
};

using AsyncFileHandleDeathTest = AsyncFileHandleTest;

TEST_F(AsyncFileHandleTest, CreateWriteReadClose) {
  createAnonymousFile();

  absl::StatusOr<size_t> writeStatus;
  bool wrote = false;
  Envoy::Buffer::OwnedImpl hello("hello");
  handle_->write(hello, 0, [&writeStatus, &wrote](absl::StatusOr<size_t> status) {
    wrote = true;
    writeStatus = std::move(status);
  });
  EXPECT_FALSE(wrote);
  advanceQueue();
  EXPECT_TRUE(wrote);
  ASSERT_EQ(absl::OkStatus(), writeStatus.status());
  EXPECT_EQ(5U, writeStatus.value());
  // Make sure writing at an offset works
  Envoy::Buffer::OwnedImpl two_chars("p!");
  handle_->write(two_chars, 3, [&writeStatus, &wrote](absl::StatusOr<size_t> status) {
    wrote = true;
    writeStatus = std::move(status);
  });
  advanceQueue();
  EXPECT_EQ(2U, writeStatus.value());

  absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> readStatus;
  handle_->read(0, 5,
                [&readStatus](absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> status) {
                  readStatus = std::move(status);
                });
  advanceQueue();
  // This should be "hello" from the first write, with the last two characters replaced with "p!"
  // from the second write.
  ASSERT_EQ(absl::OkStatus(), readStatus.status());
  EXPECT_EQ("help!", readStatus.value()->toString());
  // Verify reading at an offset.
  handle_->read(2, 3,
                [&readStatus](absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> status) {
                  readStatus = std::move(status);
                });
  advanceQueue();
  ASSERT_EQ(absl::OkStatus(), readStatus.status());
  EXPECT_EQ("lp!", readStatus.value()->toString());
  close();
}

#ifndef NDEBUG
TEST_F(AsyncFileHandleDeathTest, EnqueueWhileEnqueuedAsserts) {
  handle_->whenReady([](absl::Status) {});
  EXPECT_DEATH({ handle_->whenReady([](absl::Status) {}); }, "assert failure");
}
#endif

TEST_F(AsyncFileHandleTest, LinkWhenNotOpenFails) {
  absl::Status linkStatus;
  handle_->createHardLink("/tmp/doesnt_matter",
                          [&linkStatus](absl::Status status) { linkStatus = status; });
  advanceQueue();
  EXPECT_EQ(absl::StatusCode::kFailedPrecondition, linkStatus.code()) << linkStatus;
}

TEST_F(AsyncFileHandleTest, LinkCreatesNamedFile) {
  createAnonymousFile();
  absl::StatusOr<size_t> writeStatus;
  // Write "hello" to the anonymous file.
  Envoy::Buffer::OwnedImpl data("hello");
  handle_->write(data, 0, [&writeStatus](absl::StatusOr<size_t> status) { writeStatus = status; });
  advanceQueue();
  ASSERT_EQ(absl::OkStatus(), writeStatus.status());
  ASSERT_EQ(5U, writeStatus.value());
  char filename[1024];
  snprintf(filename, sizeof(filename), "%s/async_link_test.XXXXXX", tmpdir_.c_str());
  // Have to use `mkstemp` even though we actually only wanted a filename
  // for the purpose of this test, because `mktemp` has aggressive warnings.
  int fd = mkstemp(filename);
  ASSERT_GT(strlen(filename), 0);
  ASSERT_NE(-1, fd);
  ::close(fd);
  // Delete the file so we're where we would have been with `mktemp`!
  unlink(filename);

  // Link the anonymous file into a tmp file.
  absl::Status linkStatus;
  std::cout << "Linking as " << filename << std::endl;
  handle_->createHardLink(std::string(filename),
                          [&linkStatus](absl::Status status) { linkStatus = status; });
  advanceQueue();
  ASSERT_EQ(absl::OkStatus(), linkStatus);
  // Read the contents of the linked file back, raw.
  char fileContents[6];
  fileContents[5] = '\0';
  fd = ::open(filename, O_RDONLY);
  ASSERT_NE(-1, fd) << Envoy::errorDetails(errno);
  EXPECT_EQ(5, ::read(fd, fileContents, 5)) << Envoy::errorDetails(errno);
  // The read contents should match what we wrote.
  EXPECT_EQ(std::string("hello"), fileContents);
  ::close(fd);
  std::cout << "Removing " << filename << std::endl;
  ::unlink(filename);
}

TEST_F(AsyncFileHandleTest, LinkReturnsErrorIfLinkFails) {
  createAnonymousFile();
  absl::Status linkStatus;
  handle_->createHardLink(std::string("/some/path/that/does/not/exist"),
                          [&linkStatus](absl::Status status) { linkStatus = status; });
  advanceQueue();
  ASSERT_EQ(absl::StatusCode::kNotFound, linkStatus.code()) << linkStatus;
}

TEST_F(AsyncFileHandleTest, CreateWhenOpenFails) {
  createAnonymousFile();
  absl::Status createStatus;
  handle_->createAnonymousFile(tmpdir_,
                               [&createStatus](absl::Status status) { createStatus = status; });
  advanceQueue();
  EXPECT_EQ(absl::StatusCode::kAlreadyExists, createStatus.code());
  close();
}

TEST_F(AsyncFileHandleTest, OpenExistingWhenAlreadyOpenFails) {
  createAnonymousFile();
  auto openStatus = openExistingFile("/tmp/doesnt_matter", AsyncFileContext::Mode::READONLY);
  EXPECT_EQ(absl::StatusCode::kAlreadyExists, openStatus.code()) << openStatus;
}

TEST_F(AsyncFileHandleTest, CreateThatFailsReportsFailure) {
  absl::Status createStatus;
  handle_->createAnonymousFile(tmpdir_ + "/xyz/file/path/that/does/not/exist",
                               [&createStatus](absl::Status status) { createStatus = status; });
  advanceQueue();
  EXPECT_EQ(absl::StatusCode::kNotFound, createStatus.code());
}

TEST_F(AsyncFileHandleTest, CloseWhenNotOpenFails) {
  absl::Status closeStatus;
  handle_->close([&closeStatus](absl::Status status) { closeStatus = status; });
  advanceQueue();
  EXPECT_EQ(absl::StatusCode::kFailedPrecondition, closeStatus.code());
}

TEST_F(AsyncFileHandleTest, CloseFailureReportsError) {
  createAnonymousFile();
  forciblyCloseFileDescriptor();
  absl::Status closeStatus;
  handle_->close([&closeStatus](absl::Status status) { closeStatus = status; });
  advanceQueue();
  EXPECT_EQ(absl::StatusCode::kFailedPrecondition, closeStatus.code());
}

TEST_F(AsyncFileHandleTest, ReadWhenNotOpenFails) {
  absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> readStatus;
  handle_->read(0, 4,
                [&readStatus](absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> status) {
                  readStatus = std::move(status);
                });
  advanceQueue();
  EXPECT_EQ(absl::StatusCode::kFailedPrecondition, readStatus.status().code());
}

TEST_F(AsyncFileHandleTest, ReadFailureReportsError) {
  createAnonymousFile();
  forciblyCloseFileDescriptor();
  absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> readStatus;
  handle_->read(0, 4,
                [&readStatus](absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> status) {
                  readStatus = std::move(status);
                });
  advanceQueue();
  EXPECT_EQ(absl::StatusCode::kFailedPrecondition, readStatus.status().code());
}

TEST_F(AsyncFileHandleTest, WriteWhenNotOpenFails) {
  absl::StatusOr<size_t> writeStatus;
  Envoy::Buffer::OwnedImpl hello("hello");
  handle_->write(
      hello, 0, [&writeStatus](absl::StatusOr<size_t> status) { writeStatus = std::move(status); });
  advanceQueue();
  EXPECT_EQ(absl::StatusCode::kFailedPrecondition, writeStatus.status().code());
}

TEST_F(AsyncFileHandleTest, WriteFailureReportsError) {
  createAnonymousFile();
  forciblyCloseFileDescriptor();
  absl::StatusOr<size_t> writeStatus;
  Envoy::Buffer::OwnedImpl hello("hello");
  handle_->write(
      hello, 0, [&writeStatus](absl::StatusOr<size_t> status) { writeStatus = std::move(status); });
  advanceQueue();
  EXPECT_EQ(absl::StatusCode::kFailedPrecondition, writeStatus.status().code());
}

TEST_F(AsyncFileHandleTest, ChainedActionsWork) {
  absl::StatusOr<size_t> writeStatus;
  handle_->whenReady([&](absl::Status status) {
    ASSERT_EQ(absl::OkStatus(), status);
    handle_->createAnonymousFile(tmpdir_, [&](absl::Status status) {
      ASSERT_EQ(absl::OkStatus(), status);
      Envoy::Buffer::OwnedImpl hello("hello");
      handle_->write(hello, 0,
                     [&](absl::StatusOr<size_t> status) { writeStatus = std::move(status); });
    });
  });
  advanceQueue();
  EXPECT_EQ(5U, writeStatus.value());
  close();
}

TEST_F(AsyncFileHandleTest, AbortingUnusedFileQueuesNoOp) {
  EXPECT_EQ(0U, total_actions_queued_);
  handle_->abort();
  EXPECT_EQ(1U, total_actions_queued_);
  EXPECT_TRUE(fileWasClosed());
  advanceQueue();
  EXPECT_TRUE(fileWasClosed());
}

TEST_F(AsyncFileHandleTest, AbortingOpenFileClosesTheFile) {
  createAnonymousFile();
  EXPECT_EQ(0U, queue_.size());
  handle_->abort();
  EXPECT_EQ(1U, queue_.size());
  EXPECT_FALSE(fileWasClosed());
  advanceQueue();
  EXPECT_TRUE(fileWasClosed());
}

TEST_F(AsyncFileHandleTest, AbortingDuringExecutionCancelsTheCallback) {
  enqueueBlocker();
  auto context = popQueue();
  std::thread thread([this, context] { resolveContext(context); });
  blocker_->waitUntilExecutionBlocked();
  handle_->abort();
  blocker_->unblockExecution();
  // If abort didn't cancel the callback then join would block forever.
  thread.join();
  EXPECT_FALSE(blocker_callback_was_called_);
}

TEST_F(AsyncFileHandleTest, AbortingBeforeExecutionCancelsTheExecution) {
  enqueueBlocker();
  handle_->abort();
  // This would block forever and therefore fail the test if the execution still occurred.
  advanceQueue();
  EXPECT_FALSE(blocker_callback_was_called_);
}

TEST_F(AsyncFileHandleTest, AbortingDuringCallbackBlocksUntilCallbackCompletes) {
  enqueueBlocker();
  auto context = popQueue();
  std::thread thread([this, context] { resolveContext(context); });
  blocker_->waitUntilExecutionBlocked();
  blocker_->unblockExecution();
  blocker_->waitUntilCallbackBlocked();
  std::thread thread2([this] {
    waitForAborting();
    blocker_->unblockCallback();
  });
  handle_->abort();
  thread.join();
  thread2.join();
  EXPECT_TRUE(blocker_callback_was_called_);
}

TEST_F(AsyncFileHandleTest, DestroyingFileClosesTheDescriptor) {
  createAnonymousFile();
  int fd = fileDescriptor();
  handle_.reset();
  // Attempting to close the file descriptor should fail as it
  // should have already been closed by ~AsyncFileContextImpl.
  EXPECT_EQ(-1, ::close(fd));
  EXPECT_EQ(EBADF, errno);
}

TEST_F(AsyncFileHandleTest, OpenExistingFailsForMissingFile) {
  auto status = openExistingFile(std::string("/tmp/a_file_that_does_not_exist"),
                                 AsyncFileContext::Mode::READONLY);
  EXPECT_EQ(absl::StatusCode::kNotFound, status.code()) << status;
}

class TestTmpFile {
public:
  TestTmpFile(const std::string& tmpdir) {
    snprintf(template_, sizeof(template_), "%s/async_file_test.XXXXXX", tmpdir.c_str());
    fd_ = mkstemp(template_);
    ASSERT(fd_ > -1);
    int wrote = ::write(fd_, "hello", 5);
    ASSERT(wrote == 5);
  }
  ~TestTmpFile() {
    ::close(fd_);
    ::unlink(template_);
  }
  std::string name() { return std::string(template_); }

private:
  int fd_;
  char template_[1024];
};

TEST_F(AsyncFileHandleTest, OpenExistingWriteOnlyFailsOnRead) {
  // tmpfile is initialized to contain "hello".
  TestTmpFile tmpfile(tmpdir_);

  ASSERT_EQ(absl::OkStatus(), openExistingFile(tmpfile.name(), AsyncFileContext::Mode::WRITEONLY));
  absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> readStatus;
  handle_->read(0, 5,
                [&readStatus](absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> status) {
                  readStatus = std::move(status);
                });
  advanceQueue();
  ASSERT_EQ(absl::StatusCode::kFailedPrecondition, readStatus.status().code())
      << readStatus.status();
}

TEST_F(AsyncFileHandleTest, OpenExistingWriteOnlyCanWrite) {
  // tmpfile is initialized to contain "hello".
  TestTmpFile tmpfile(tmpdir_);

  ASSERT_EQ(absl::OkStatus(), openExistingFile(tmpfile.name(), AsyncFileContext::Mode::WRITEONLY));
  absl::StatusOr<size_t> writeStatus;
  Envoy::Buffer::OwnedImpl buf("nine char");
  handle_->write(
      buf, 0, [&writeStatus](absl::StatusOr<size_t> status) { writeStatus = std::move(status); });
  advanceQueue();
  ASSERT_EQ(9, writeStatus.value());
}

TEST_F(AsyncFileHandleTest, OpenExistingReadOnlyFailsOnWrite) {
  // tmpfile is initialized to contain "hello".
  TestTmpFile tmpfile(tmpdir_);

  ASSERT_EQ(absl::OkStatus(), openExistingFile(tmpfile.name(), AsyncFileContext::Mode::READONLY));
  absl::StatusOr<size_t> writeStatus;
  Envoy::Buffer::OwnedImpl buf("hello");
  handle_->write(
      buf, 0, [&writeStatus](absl::StatusOr<size_t> status) { writeStatus = std::move(status); });
  advanceQueue();
  ASSERT_EQ(absl::StatusCode::kFailedPrecondition, writeStatus.status().code())
      << writeStatus.status();
}

TEST_F(AsyncFileHandleTest, OpenExistingReadOnlyCanRead) {
  // tmpfile is initialized to contain "hello".
  TestTmpFile tmpfile(tmpdir_);

  ASSERT_EQ(absl::OkStatus(), openExistingFile(tmpfile.name(), AsyncFileContext::Mode::READONLY));
  absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> readStatus;
  handle_->read(0, 5,
                [&readStatus](absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> status) {
                  readStatus = std::move(status);
                });
  advanceQueue();
  ASSERT_EQ("hello", readStatus.value()->toString());
}

TEST_F(AsyncFileHandleTest, OpenExistingReadWriteCanReadAndWrite) {
  // tmpfile is initialized to contain "hello".
  TestTmpFile tmpfile(tmpdir_);

  ASSERT_EQ(absl::OkStatus(), openExistingFile(tmpfile.name(), AsyncFileContext::Mode::READWRITE));
  absl::StatusOr<size_t> writeStatus;
  Envoy::Buffer::OwnedImpl buf("p me!");
  handle_->write(
      buf, 3, [&writeStatus](absl::StatusOr<size_t> status) { writeStatus = std::move(status); });
  advanceQueue();
  ASSERT_EQ(absl::OkStatus(), writeStatus.status());
  ASSERT_EQ(5, writeStatus.value());
  absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> readStatus;
  handle_->read(0, 8,
                [&readStatus](absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> status) {
                  readStatus = std::move(status);
                });
  advanceQueue();
  ASSERT_EQ(absl::OkStatus(), readStatus.status());
  ASSERT_EQ("help me!", readStatus.value()->toString());
}

TEST_F(AsyncFileHandleTest, UnlinkDeletesTheNamedFile) {
  TestTmpFile file(tmpdir_);
  absl::Status unlinkStatus;
  handle_->unlink(file.name(), [&unlinkStatus](absl::Status status) { unlinkStatus = status; });
  advanceQueue();
  EXPECT_EQ(absl::OkStatus(), unlinkStatus);
  EXPECT_EQ(-1, ::access(file.name().c_str(), R_OK))
      << file.name() << " should be deleted, but it still exists";
}

TEST_F(AsyncFileHandleTest, UnlinkReturnsErrorIfOperationFails) {
  TestTmpFile file(tmpdir_);
  ASSERT_EQ(0, ::unlink(file.name().c_str()));
  absl::Status unlinkStatus;
  handle_->unlink(file.name(), [&unlinkStatus](absl::Status status) { unlinkStatus = status; });
  advanceQueue();
  EXPECT_EQ(absl::StatusCode::kNotFound, unlinkStatus.code()) << unlinkStatus;
}

TEST_F(AsyncFileHandleTest, DuplicateCreatesIndependentHandle) {
  createAnonymousFile();
  auto newFileStatus = handle_->duplicate();
  ASSERT_EQ(absl::OkStatus(), newFileStatus.status());
  AsyncFileHandle newFile = std::move(newFileStatus.value());
  // Close the original file.
  close();
  absl::StatusOr<size_t> writeStatus;
  Envoy::Buffer::OwnedImpl buf("hello");
  newFile->write(buf, 0, [&writeStatus](absl::StatusOr<size_t> result) { writeStatus = result; });
  advanceQueue();
  // writing to the duplicate file should still work.
  ASSERT_EQ(absl::OkStatus(), writeStatus.status());
  EXPECT_EQ(5, writeStatus.value());
  absl::Status closeStatus;
  newFile->close([&closeStatus](absl::Status result) { closeStatus = result; });
  advanceQueue();
  EXPECT_EQ(absl::OkStatus(), closeStatus);
}

TEST_F(AsyncFileHandleTest, DuplicateFailsOnNonOpenHandle) {
  auto newFileStatus = handle_->duplicate();
  ASSERT_EQ(absl::StatusCode::kFailedPrecondition, newFileStatus.status().code())
      << newFileStatus.status();
}

TEST_F(AsyncFileHandleTest, DuplicateFailsOnInternalError) {
  createAnonymousFile();
  // Get a pointer to the implementation so we can fake a bad file descriptor.
  auto cheat_handle = dynamic_cast<AsyncFileContextBasic*>(handle_.get());
  int original_fd = cheat_handle->file_descriptor_;
  cheat_handle->file_descriptor_ = 65535;
  auto newFileStatus = handle_->duplicate();
  EXPECT_EQ(absl::StatusCode::kFailedPrecondition, newFileStatus.status().code())
      << newFileStatus.status();
  // Restore the file descriptor so we can close it cleanly.
  cheat_handle->file_descriptor_ = original_fd;
  close();
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
