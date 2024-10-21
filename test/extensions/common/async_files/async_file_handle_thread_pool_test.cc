#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "envoy/extensions/common/async_files/v3/async_file_manager.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/common/async_files/async_file_action.h"
#include "source/extensions/common/async_files/async_file_handle.h"
#include "source/extensions/common/async_files/async_file_manager.h"
#include "source/extensions/common/async_files/async_file_manager_factory.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/status_utility.h"

#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

using StatusHelpers::IsOkAndHolds;
using StatusHelpers::StatusIs;
using ::testing::_;
using ::testing::Eq;
using ::testing::Return;
using ::testing::StrictMock;

class AsyncFileHandleHelpers {
public:
  void resolveFileActions() {
    manager_->waitForIdle();
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }
  void close(AsyncFileHandle& handle) {
    absl::Status close_result;
    EXPECT_OK(
        handle->close(dispatcher_.get(), [&](absl::Status status) { close_result = status; }));
    resolveFileActions();
    EXPECT_OK(close_result);
  }
  AsyncFileHandle createAnonymousFile() {
    AsyncFileHandle create_result;
    manager_->createAnonymousFile(
        dispatcher_.get(), tmpdir_,
        [&](absl::StatusOr<AsyncFileHandle> result) { create_result = result.value(); });
    resolveFileActions();
    return create_result;
  }
  AsyncFileHandle openExistingFile(absl::string_view filename, AsyncFileManager::Mode mode) {
    AsyncFileHandle open_result;
    manager_->openExistingFile(
        dispatcher_.get(), filename, mode,
        [&](absl::StatusOr<AsyncFileHandle> result) { open_result = result.value(); });
    resolveFileActions();
    return open_result;
  }
  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  std::string tmpdir_ = test_tmpdir ? test_tmpdir : "/tmp";

  std::unique_ptr<Singleton::ManagerImpl> singleton_manager_ =
      std::make_unique<Singleton::ManagerImpl>();
  std::shared_ptr<AsyncFileManagerFactory> factory_ =
      AsyncFileManagerFactory::singleton(singleton_manager_.get());
  std::shared_ptr<AsyncFileManager> manager_;
  Api::ApiPtr api_ = Api::createApiForTest();
  Event::DispatcherPtr dispatcher_ = api_->allocateDispatcher("test_thread");
};

class AsyncFileHandleTest : public testing::Test, public AsyncFileHandleHelpers {
public:
  void SetUp() override {
    envoy::extensions::common::async_files::v3::AsyncFileManagerConfig config;
    config.mutable_thread_pool()->set_thread_count(1);
    manager_ = factory_->getAsyncFileManager(config);
  }
};

class AsyncFileHandleWithMockPosixTest : public testing::Test, public AsyncFileHandleHelpers {
public:
  void SetUp() override {
    EXPECT_CALL(mock_posix_file_operations_, supportsAllPosixFileOperations())
        .WillRepeatedly(Return(true));
    envoy::extensions::common::async_files::v3::AsyncFileManagerConfig config;
    config.mutable_thread_pool()->set_thread_count(1);
    manager_ = factory_->getAsyncFileManager(config, &mock_posix_file_operations_);
    EXPECT_CALL(mock_posix_file_operations_, open(_, _, _))
        .WillRepeatedly(Return(Api::SysCallIntResult{1, 0}));
    EXPECT_CALL(mock_posix_file_operations_, open(_, _))
        .WillRepeatedly(Return(Api::SysCallIntResult{1, 0}));
    EXPECT_CALL(mock_posix_file_operations_, close(_))
        .WillRepeatedly(Return(Api::SysCallIntResult{0, 0}));
  }
  void TearDown() override {
    // manager_ must be torn down before mock_posix_file_operations_ to ensure that file
    // operations are completed before the mock is destroyed.
    manager_ = nullptr;
    factory_ = nullptr;
  }
  StrictMock<Api::MockOsSysCalls> mock_posix_file_operations_;
};

TEST_F(AsyncFileHandleTest, WriteReadClose) {
  auto handle = createAnonymousFile();
  absl::StatusOr<size_t> write_status, second_write_status;
  absl::StatusOr<Buffer::InstancePtr> read_status, second_read_status;
  Buffer::OwnedImpl hello("hello");
  ASSERT_OK(handle->write(dispatcher_.get(), hello, 0, [&](absl::StatusOr<size_t> status) {
    write_status = std::move(status);
  }));
  resolveFileActions();
  EXPECT_THAT(write_status, IsOkAndHolds(5U));
  Buffer::OwnedImpl two_chars("p!");
  ASSERT_OK(handle->write(dispatcher_.get(), two_chars, 3, [&](absl::StatusOr<size_t> status) {
    second_write_status = std::move(status);
  }));
  resolveFileActions();
  EXPECT_THAT(second_write_status, IsOkAndHolds(2U));
  ASSERT_OK(handle->read(dispatcher_.get(), 0, 5, [&](absl::StatusOr<Buffer::InstancePtr> status) {
    read_status = std::move(status);
  }));
  resolveFileActions();
  ASSERT_OK(read_status);
  EXPECT_THAT(*read_status.value(), BufferStringEqual("help!"));
  ASSERT_OK(handle->read(dispatcher_.get(), 2, 3, [&](absl::StatusOr<Buffer::InstancePtr> status) {
    second_read_status = std::move(status);
  }));
  resolveFileActions();
  ASSERT_OK(second_read_status);
  EXPECT_THAT(*second_read_status.value(), BufferStringEqual("lp!"));
  close(handle);
}

TEST_F(AsyncFileHandleTest, LinkCreatesNamedFile) {
  auto handle = createAnonymousFile();
  absl::StatusOr<size_t> write_status;
  // Write "hello" to the anonymous file.
  Buffer::OwnedImpl data("hello");
  EXPECT_OK(handle->write(dispatcher_.get(), data, 0, [&](absl::StatusOr<size_t> status) {
    write_status = std::move(status);
  }));
  resolveFileActions();
  ASSERT_THAT(write_status, IsOkAndHolds(5U));
  char filename[1024];
  snprintf(filename, sizeof(filename), "%s/async_link_test.XXXXXX", tmpdir_.c_str());
  // Have to use `mkstemp` even though we actually only wanted a filename
  // for the purpose of this test, because `mktemp` has aggressive warnings.
  Api::OsSysCalls& posix = Api::OsSysCallsSingleton().get();
  int fd = posix.mkstemp(filename).return_value_;
  ASSERT_GT(strlen(filename), 0);
  ASSERT_NE(-1, fd);
  posix.close(fd);
  // Delete the file so we're where we would have been with `mktemp`!
  posix.unlink(filename);

  // Link the anonymous file into our tmp file name.
  absl::Status link_status = absl::InternalError("not set");
  std::cout << "Linking as " << filename << std::endl;

  EXPECT_OK(handle->createHardLink(dispatcher_.get(), std::string(filename),
                                   [&](absl::Status status) { link_status = status; }));
  resolveFileActions();
  ASSERT_OK(link_status);
  // Read the contents of the linked file back, raw.
  char fileContents[6];
  fileContents[5] = '\0';
  auto open = posix.open(filename, O_RDONLY);
  ASSERT_NE(-1, open.return_value_) << Envoy::errorDetails(open.errno_);
  auto read = posix.pread(open.return_value_, fileContents, 5, 0);
  EXPECT_EQ(5, read.return_value_) << Envoy::errorDetails(read.errno_);
  // The read contents should match what we wrote.
  EXPECT_EQ(std::string("hello"), fileContents);
  posix.close(open.return_value_);
  std::cout << "Removing " << filename << std::endl;
  posix.unlink(filename);
  close(handle);
}

TEST_F(AsyncFileHandleTest, LinkReturnsErrorIfLinkFails) {
  auto handle = createAnonymousFile();
  absl::Status link_status = absl::InternalError("not set");
  EXPECT_OK(handle->createHardLink(dispatcher_.get(), "/some/path/that/does/not/exist",
                                   [&](absl::Status status) { link_status = std::move(status); }));
  resolveFileActions();
  ASSERT_EQ(absl::StatusCode::kNotFound, link_status.code()) << link_status;
  close(handle);
}

class TestTmpFile {
public:
  TestTmpFile(const std::string& tmpdir) {
    snprintf(template_, sizeof(template_), "%s/async_file_test.XXXXXX", tmpdir.c_str());
    Api::OsSysCalls& posix = Api::OsSysCallsSingleton().get();
    fd_ = posix.mkstemp(template_).return_value_;
    ASSERT(fd_ > -1);
    int wrote = posix.write(fd_, "hello", 5).return_value_;
    ASSERT(wrote == 5);
  }
  ~TestTmpFile() {
    Api::OsSysCalls& posix = Api::OsSysCallsSingleton().get();
    posix.close(fd_);
    posix.unlink(template_);
  }
  std::string name() { return {template_}; }

private:
  int fd_;
  char template_[1024];
};

TEST_F(AsyncFileHandleTest, OpenExistingWriteOnlyFailsOnRead) {
  // tmpfile is initialized to contain "hello".
  TestTmpFile tmpfile(tmpdir_);

  auto handle = openExistingFile(tmpfile.name(), AsyncFileManager::Mode::WriteOnly);
  absl::StatusOr<Buffer::InstancePtr> read_status;
  EXPECT_OK(handle->read(dispatcher_.get(), 0, 5, [&](absl::StatusOr<Buffer::InstancePtr> status) {
    read_status = std::move(status);
  }));
  resolveFileActions();
  ASSERT_EQ(absl::StatusCode::kFailedPrecondition, read_status.status().code())
      << read_status.status();
  close(handle);
}

TEST_F(AsyncFileHandleTest, OpenExistingWriteOnlyCanWrite) {
  // tmpfile is initialized to contain "hello".
  TestTmpFile tmpfile(tmpdir_);

  auto handle = openExistingFile(tmpfile.name(), AsyncFileManager::Mode::WriteOnly);
  absl::StatusOr<size_t> write_status;
  Buffer::OwnedImpl buf("nine char");
  EXPECT_OK(handle->write(dispatcher_.get(), buf, 0, [&](absl::StatusOr<size_t> status) {
    write_status = std::move(status);
  }));
  resolveFileActions();
  ASSERT_OK(write_status);
  EXPECT_EQ(9, write_status.value());
  close(handle);
}

TEST_F(AsyncFileHandleTest, OpenExistingReadOnlyFailsOnWrite) {
  // tmpfile is initialized to contain "hello".
  TestTmpFile tmpfile(tmpdir_);

  auto handle = openExistingFile(tmpfile.name(), AsyncFileManager::Mode::ReadOnly);
  absl::StatusOr<size_t> write_status;
  Buffer::OwnedImpl buf("hello");
  EXPECT_OK(handle->write(dispatcher_.get(), buf, 0, [&](absl::StatusOr<size_t> status) {
    write_status = std::move(status);
  }));
  resolveFileActions();
  ASSERT_EQ(absl::StatusCode::kFailedPrecondition, write_status.status().code())
      << write_status.status();
  close(handle);
}

TEST_F(AsyncFileHandleTest, OpenExistingReadOnlyCanRead) {
  // tmpfile is initialized to contain "hello".
  TestTmpFile tmpfile(tmpdir_);

  auto handle = openExistingFile(tmpfile.name(), AsyncFileManager::Mode::ReadOnly);
  absl::StatusOr<Buffer::InstancePtr> read_status;
  EXPECT_OK(handle->read(dispatcher_.get(), 0, 5, [&](absl::StatusOr<Buffer::InstancePtr> status) {
    read_status = std::move(status);
  }));
  resolveFileActions();
  ASSERT_OK(read_status);
  ASSERT_EQ("hello", read_status.value()->toString());
  close(handle);
}

TEST_F(AsyncFileHandleTest, OpenExistingReadWriteCanReadAndWrite) {
  // tmpfile is initialized to contain "hello".
  TestTmpFile tmpfile(tmpdir_);

  auto handle = openExistingFile(tmpfile.name(), AsyncFileManager::Mode::ReadWrite);
  absl::StatusOr<size_t> write_status;
  Buffer::OwnedImpl buf("p me!");
  EXPECT_OK(handle->write(dispatcher_.get(), buf, 3, [&](absl::StatusOr<size_t> status) {
    write_status = std::move(status);
  }));
  resolveFileActions();
  ASSERT_THAT(write_status, IsOkAndHolds(5U));
  absl::StatusOr<Buffer::InstancePtr> read_status;
  EXPECT_OK(handle->read(dispatcher_.get(), 0, 8, [&](absl::StatusOr<Buffer::InstancePtr> status) {
    read_status = std::move(status);
  }));
  resolveFileActions();
  ASSERT_OK(read_status);
  EXPECT_THAT(*read_status.value(), BufferStringEqual("help me!"));
  close(handle);
}

TEST_F(AsyncFileHandleTest, DuplicateCreatesIndependentHandle) {
  auto handle = createAnonymousFile();
  absl::StatusOr<AsyncFileHandle> duplicate_status;
  EXPECT_OK(handle->duplicate(dispatcher_.get(), [&](absl::StatusOr<AsyncFileHandle> status) {
    duplicate_status = std::move(status);
  }));
  resolveFileActions();
  ASSERT_OK(duplicate_status);
  AsyncFileHandle dup_file = std::move(duplicate_status.value());
  // Close the original file.
  close(handle);
  absl::StatusOr<size_t> write_status;
  Buffer::OwnedImpl buf("hello");
  EXPECT_OK(dup_file->write(dispatcher_.get(), buf, 0, [&](absl::StatusOr<size_t> result) {
    write_status = std::move(result);
  }));
  resolveFileActions();
  // writing to the duplicate file should still work.
  EXPECT_THAT(write_status, IsOkAndHolds(5U));
  close(dup_file);
}

TEST_F(AsyncFileHandleWithMockPosixTest, PartialReadReturnsPartialResult) {
  auto handle = createAnonymousFile();
  EXPECT_CALL(mock_posix_file_operations_, pread(_, _, _, _))
      .WillOnce([](int, void* buf, size_t, off_t) {
        memcpy(buf, "hel", 3);
        return Api::SysCallSizeResult{3, 0};
      });
  absl::StatusOr<Buffer::InstancePtr> read_status;
  EXPECT_OK(handle->read(dispatcher_.get(), 0, 5, [&](absl::StatusOr<Buffer::InstancePtr> status) {
    read_status = std::move(status.value());
  }));
  resolveFileActions();
  EXPECT_OK(read_status);
  EXPECT_THAT(*read_status.value(), BufferStringEqual("hel"));
  close(handle);
}

MATCHER_P(IsMemoryMatching, str, "") {
  absl::string_view expected{str};
  *result_listener << "is memory matching " << expected;
  absl::string_view target{static_cast<const char*>(arg), expected.size()};
  return ExplainMatchResult(expected, target, result_listener);
}

TEST_F(AsyncFileHandleWithMockPosixTest, PartialWriteRetries) {
  auto handle = createAnonymousFile();
  Buffer::OwnedImpl write_value{"hello"};
  EXPECT_CALL(mock_posix_file_operations_, pwrite(_, IsMemoryMatching("hello"), 5, 0))
      .WillOnce(Return(Api::SysCallSizeResult{3, 0}));
  EXPECT_CALL(mock_posix_file_operations_, pwrite(_, IsMemoryMatching("lo"), 2, 3))
      .WillOnce(Return(Api::SysCallSizeResult{2, 0}));
  absl::StatusOr<size_t> write_status;
  EXPECT_OK(handle->write(dispatcher_.get(), write_value, 0, [&](absl::StatusOr<size_t> status) {
    write_status = std::move(status.value());
  }));
  resolveFileActions();
  EXPECT_THAT(write_status, IsOkAndHolds(5U));
  close(handle);
}

TEST_F(AsyncFileHandleWithMockPosixTest, CancellingDuplicateInProgressClosesTheFile) {
  auto handle = createAnonymousFile();
  std::promise<void> entering_dup, finishing_dup;
  EXPECT_CALL(mock_posix_file_operations_, duplicate(_)).WillOnce([&]() {
    entering_dup.set_value();
    finishing_dup.get_future().wait();
    return Api::SysCallSocketResult{4242, 0};
  });
  auto cancel_dup = handle->duplicate(dispatcher_.get(), [](absl::StatusOr<AsyncFileHandle>) {
    FAIL() << "cancelled callback should not be called";
  });
  entering_dup.get_future().wait();
  cancel_dup.value()();
  std::promise<void> closing;
  EXPECT_CALL(mock_posix_file_operations_, close(4242)).WillOnce([&]() {
    closing.set_value();
    return Api::SysCallIntResult{0, 0};
  });
  finishing_dup.set_value();
  closing.get_future().wait();
  close(handle);
}

TEST_F(AsyncFileHandleWithMockPosixTest, CancellingCreateHardLinkInProgressRemovesTheLink) {
  auto handle = createAnonymousFile();
  std::promise<void> entering_hardlink, finishing_hardlink;
  std::string filename = "irrelevant_filename";
  EXPECT_CALL(mock_posix_file_operations_, linkat(_, _, _, Eq(filename), _)).WillOnce([&]() {
    entering_hardlink.set_value();
    finishing_hardlink.get_future().wait();
    return Api::SysCallIntResult{0, 0};
  });
  auto cancel_hardlink = handle->createHardLink(dispatcher_.get(), filename, [](absl::Status) {
    FAIL() << "cancelled callback should not be called";
  });
  entering_hardlink.get_future().wait();
  cancel_hardlink.value()();
  std::promise<void> unlinking;
  EXPECT_CALL(mock_posix_file_operations_, unlink(Eq(filename))).WillOnce([&]() {
    unlinking.set_value();
    return Api::SysCallIntResult{0, 0};
  });
  finishing_hardlink.set_value();
  unlinking.get_future().wait();
  close(handle);
}

TEST_F(AsyncFileHandleWithMockPosixTest, CancellingFailedCreateHardLinkInProgressDoesNotUnlink) {
  auto handle = createAnonymousFile();
  std::promise<void> entering_hardlink, finishing_hardlink;
  std::string filename = "irrelevant_filename";
  EXPECT_CALL(mock_posix_file_operations_, linkat(_, _, _, Eq(filename), _)).WillOnce([&]() {
    entering_hardlink.set_value();
    finishing_hardlink.get_future().wait();
    return Api::SysCallIntResult{-1, EBADF};
  });
  auto cancel_hardlink = handle->createHardLink(dispatcher_.get(), filename, [](absl::Status) {
    FAIL() << "cancelled callback should not be called";
  });
  entering_hardlink.get_future().wait();
  cancel_hardlink.value()();
  EXPECT_CALL(mock_posix_file_operations_, unlink(_)).Times(0);
  finishing_hardlink.set_value();
  std::this_thread::yield();
  close(handle);
}

TEST_F(AsyncFileHandleWithMockPosixTest, StatSuccessReturnsPopulatedStatStruct) {
  auto handle = createAnonymousFile();
  struct stat expected_stat = {};
  expected_stat.st_size = 9876;
  EXPECT_CALL(mock_posix_file_operations_, fstat(_, _)).WillOnce([&](int, struct stat* buffer) {
    *buffer = expected_stat;
    return Api::SysCallIntResult{0, 0};
  });
  absl::StatusOr<struct stat> fstat_status;
  EXPECT_OK(handle->stat(dispatcher_.get(), [&](absl::StatusOr<struct stat> status) {
    fstat_status = std::move(status);
  }));
  resolveFileActions();
  EXPECT_THAT(fstat_status, IsOkAndHolds(testing::Field(&stat::st_size, expected_stat.st_size)));
  close(handle);
}

TEST_F(AsyncFileHandleWithMockPosixTest, StatFailureReportsError) {
  auto handle = createAnonymousFile();
  EXPECT_CALL(mock_posix_file_operations_, fstat(_, _))
      .WillOnce(Return(Api::SysCallIntResult{-1, EBADF}));
  absl::StatusOr<struct stat> fstat_status;
  EXPECT_OK(handle->stat(dispatcher_.get(), [&](absl::StatusOr<struct stat> status) {
    fstat_status = std::move(status);
  }));
  resolveFileActions();
  EXPECT_THAT(fstat_status, StatusIs(absl::StatusCode::kFailedPrecondition));
  close(handle);
}

TEST_F(AsyncFileHandleWithMockPosixTest, CloseFailureReportsError) {
  auto handle = createAnonymousFile();
  EXPECT_CALL(mock_posix_file_operations_, close(1))
      .WillOnce(Return(Api::SysCallIntResult{-1, EBADF}));
  absl::Status close_status;
  EXPECT_OK(handle->close(dispatcher_.get(),
                          [&](absl::Status status) { close_status = std::move(status); }));
  resolveFileActions();
  EXPECT_EQ(absl::StatusCode::kFailedPrecondition, close_status.code()) << close_status;
}

TEST_F(AsyncFileHandleWithMockPosixTest, DuplicateFailureReportsError) {
  auto handle = createAnonymousFile();
  EXPECT_CALL(mock_posix_file_operations_, duplicate(_))
      .WillOnce(Return(Api::SysCallIntResult{-1, EBADF}));
  absl::StatusOr<AsyncFileHandle> dup_status;
  EXPECT_OK(handle->duplicate(dispatcher_.get(), [&](absl::StatusOr<AsyncFileHandle> status) {
    dup_status = std::move(status);
  }));
  resolveFileActions();
  EXPECT_THAT(dup_status, StatusIs(absl::StatusCode::kFailedPrecondition));
  close(handle);
}

TEST_F(AsyncFileHandleWithMockPosixTest, EnqueuingActionAfterCloseReturnsError) {
  auto handle = createAnonymousFile();
  EXPECT_OK(handle->close(dispatcher_.get(), [](absl::Status) {}));
  auto failed_status = handle->close(dispatcher_.get(), [](absl::Status) {});
  EXPECT_THAT(failed_status, StatusIs(absl::StatusCode::kFailedPrecondition));
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
