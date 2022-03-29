#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/common/async_files/async_file_action.h"
#include "source/extensions/common/async_files/async_file_handle.h"
#include "source/extensions/common/async_files/async_file_manager.h"

#include "test/mocks/extensions/common/async_files/mock_posix_file_operations.h"

#include "absl/status/statusor.h"
#include "absl/synchronization/barrier.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Eq;
using ::testing::Return;

class AsyncFileHandleHelpers {
public:
  void close(AsyncFileHandle& handle) {
    absl::Barrier close_barrier{2};
    absl::Status close_status;
    handle->close([&close_barrier, &close_status](absl::Status status) {
      close_status = status;
      close_barrier.Block();
    });
    close_barrier.Block();
    EXPECT_EQ(absl::OkStatus(), close_status);
  }
  AsyncFileHandle createAnonymousFile() {
    absl::Barrier barrier{2};
    AsyncFileHandle handle;
    manager_->createAnonymousFile(tmpdir_, [&](absl::StatusOr<AsyncFileHandle> result) {
      handle = result.value();
      barrier.Block();
    });
    barrier.Block();
    return handle;
  }
  AsyncFileHandle openExistingFile(absl::string_view filename, AsyncFileManager::Mode mode) {
    absl::Barrier barrier{2};
    AsyncFileHandle handle;
    manager_->openExistingFile(filename, mode, [&](absl::StatusOr<AsyncFileHandle> result) {
      handle = result.value();
      barrier.Block();
    });
    barrier.Block();
    return handle;
  }
  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  std::string tmpdir_ = test_tmpdir ? test_tmpdir : "/tmp";
  std::unique_ptr<AsyncFileManager> manager_;
};

class AsyncFileHandleTest : public testing::Test, public AsyncFileHandleHelpers {
public:
  void SetUp() override {
    AsyncFileManagerConfig config;
    config.thread_pool_size = 1;
    manager_ = config.createManager();
  }
};

class AsyncFileHandleWithMockPosixTest : public testing::Test, public AsyncFileHandleHelpers {
public:
  void SetUp() override {
    AsyncFileManagerConfig config;
    config.thread_pool_size = 1;
    config.substitute_posix_file_operations = &mock_posix_file_operations_;
    manager_ = config.createManager();
    EXPECT_CALL(mock_posix_file_operations_, open(_, _, _))
        .Times(AnyNumber())
        .WillRepeatedly(Return(1));
    EXPECT_CALL(mock_posix_file_operations_, open(_, _))
        .Times(AnyNumber())
        .WillRepeatedly(Return(1));
    EXPECT_CALL(mock_posix_file_operations_, close(_)).Times(AnyNumber()).WillRepeatedly(Return(0));
  }
  MockPosixFileOperations mock_posix_file_operations_;
};

TEST_F(AsyncFileHandleTest, WriteReadClose) {
  auto handle = createAnonymousFile();
  absl::StatusOr<size_t> write_status, second_write_status;
  absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> read_status, second_read_status;
  absl::Status close_status;
  Envoy::Buffer::OwnedImpl hello("hello");
  absl::Barrier done_barrier{2};
  handle->write(hello, 0, [&](absl::StatusOr<size_t> status) {
    write_status = std::move(status);
    // Make sure writing at an offset works
    Envoy::Buffer::OwnedImpl two_chars("p!");
    handle->write(two_chars, 3, [&](absl::StatusOr<size_t> status) {
      second_write_status = std::move(status);
      handle->read(0, 5, [&](absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> status) {
        read_status = std::move(status);
        // Verify reading at an offset.
        handle->read(2, 3, [&](absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> status) {
          second_read_status = std::move(status);
          handle->close([&](absl::Status status) {
            close_status = std::move(status);
            done_barrier.Block();
          });
        });
      });
    });
  });
  done_barrier.Block();
  ASSERT_EQ(absl::OkStatus(), write_status.status());
  ASSERT_EQ(absl::OkStatus(), second_write_status.status());
  ASSERT_EQ(absl::OkStatus(), read_status.status());
  ASSERT_EQ(absl::OkStatus(), second_read_status.status());
  ASSERT_EQ(absl::OkStatus(), close_status);
  // The first write was 5 characters.
  EXPECT_EQ(5U, write_status.value());

  // The second write was 2 characters.
  EXPECT_EQ(2U, second_write_status.value());

  // This should be "hello" from the first write, with the last two characters replaced with "p!"
  // from the second write.
  EXPECT_EQ("help!", read_status.value()->toString());

  // Second read should have three characters in it.
  EXPECT_EQ("lp!", second_read_status.value()->toString());
}

TEST_F(AsyncFileHandleTest, LinkCreatesNamedFile) {
  auto handle = createAnonymousFile();
  absl::StatusOr<size_t> write_status;
  // Write "hello" to the anonymous file.
  Envoy::Buffer::OwnedImpl data("hello");
  absl::Barrier write_barrier{2};
  handle->write(data, 0, [&write_status, &write_barrier](absl::StatusOr<size_t> status) {
    write_status = status;
    write_barrier.Block();
  });
  write_barrier.Block();
  ASSERT_EQ(absl::OkStatus(), write_status.status());
  ASSERT_EQ(5U, write_status.value());
  char filename[1024];
  snprintf(filename, sizeof(filename), "%s/async_link_test.XXXXXX", tmpdir_.c_str());
  // Have to use `mkstemp` even though we actually only wanted a filename
  // for the purpose of this test, because `mktemp` has aggressive warnings.
  int fd = ::mkstemp(filename);
  ASSERT_GT(strlen(filename), 0);
  ASSERT_NE(-1, fd);
  ::close(fd);
  // Delete the file so we're where we would have been with `mktemp`!
  ::unlink(filename);

  // Link the anonymous file into our tmp file name.
  absl::Status link_status;
  absl::Barrier link_barrier{2};
  std::cout << "Linking as " << filename << std::endl;

  handle->createHardLink(std::string(filename), [&link_status, &link_barrier](absl::Status status) {
    link_status = status;
    link_barrier.Block();
  });
  link_barrier.Block();
  ASSERT_EQ(absl::OkStatus(), link_status);
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
  close(handle);
}

TEST_F(AsyncFileHandleTest, LinkReturnsErrorIfLinkFails) {
  auto handle = createAnonymousFile();
  absl::Status link_status;
  absl::Barrier link_barrier{2};
  handle->createHardLink("/some/path/that/does/not/exist", [&](absl::Status status) {
    link_status = status;
    link_barrier.Block();
  });
  link_barrier.Block();
  ASSERT_EQ(absl::StatusCode::kNotFound, link_status.code()) << link_status;
  close(handle);
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

  auto handle = openExistingFile(tmpfile.name(), AsyncFileManager::Mode::WriteOnly);
  absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> read_status;
  absl::Barrier read_barrier{2};
  handle->read(0, 5, [&](absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> status) {
    read_status = std::move(status);
    read_barrier.Block();
  });
  read_barrier.Block();
  ASSERT_EQ(absl::StatusCode::kFailedPrecondition, read_status.status().code())
      << read_status.status();
  close(handle);
}

TEST_F(AsyncFileHandleTest, OpenExistingWriteOnlyCanWrite) {
  // tmpfile is initialized to contain "hello".
  TestTmpFile tmpfile(tmpdir_);

  auto handle = openExistingFile(tmpfile.name(), AsyncFileManager::Mode::WriteOnly);
  absl::StatusOr<size_t> write_status;
  absl::Barrier write_barrier{2};
  Envoy::Buffer::OwnedImpl buf("nine char");
  handle->write(buf, 0, [&](absl::StatusOr<size_t> status) {
    write_status = std::move(status);
    write_barrier.Block();
  });
  write_barrier.Block();
  ASSERT_EQ(9, write_status.value());
  close(handle);
}

TEST_F(AsyncFileHandleTest, OpenExistingReadOnlyFailsOnWrite) {
  // tmpfile is initialized to contain "hello".
  TestTmpFile tmpfile(tmpdir_);

  auto handle = openExistingFile(tmpfile.name(), AsyncFileManager::Mode::ReadOnly);
  absl::StatusOr<size_t> write_status;
  absl::Barrier write_barrier{2};
  Envoy::Buffer::OwnedImpl buf("hello");
  handle->write(buf, 0, [&](absl::StatusOr<size_t> status) {
    write_status = std::move(status);
    write_barrier.Block();
  });
  write_barrier.Block();
  ASSERT_EQ(absl::StatusCode::kFailedPrecondition, write_status.status().code())
      << write_status.status();
  close(handle);
}

TEST_F(AsyncFileHandleTest, OpenExistingReadOnlyCanRead) {
  // tmpfile is initialized to contain "hello".
  TestTmpFile tmpfile(tmpdir_);

  auto handle = openExistingFile(tmpfile.name(), AsyncFileManager::Mode::ReadOnly);
  absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> read_status;
  absl::Barrier read_barrier{2};
  handle->read(0, 5, [&](absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> status) {
    read_status = std::move(status);
    read_barrier.Block();
  });
  read_barrier.Block();
  ASSERT_EQ("hello", read_status.value()->toString());
  close(handle);
}

TEST_F(AsyncFileHandleTest, OpenExistingReadWriteCanReadAndWrite) {
  // tmpfile is initialized to contain "hello".
  TestTmpFile tmpfile(tmpdir_);

  auto handle = openExistingFile(tmpfile.name(), AsyncFileManager::Mode::ReadWrite);
  absl::StatusOr<size_t> write_status;
  absl::Barrier write_barrier{2};
  Envoy::Buffer::OwnedImpl buf("p me!");
  handle->write(buf, 3, [&](absl::StatusOr<size_t> status) {
    write_status = std::move(status);
    write_barrier.Block();
  });
  write_barrier.Block();
  ASSERT_EQ(absl::OkStatus(), write_status.status());
  ASSERT_EQ(5, write_status.value());
  absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> read_status;
  absl::Barrier read_barrier{2};
  handle->read(0, 8, [&](absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> status) {
    read_status = std::move(status);
    read_barrier.Block();
  });
  read_barrier.Block();
  ASSERT_EQ(absl::OkStatus(), read_status.status());
  ASSERT_EQ("help me!", read_status.value()->toString());
  close(handle);
}

TEST_F(AsyncFileHandleTest, DuplicateCreatesIndependentHandle) {
  auto handle = createAnonymousFile();
  absl::StatusOr<AsyncFileHandle> duplicate_status;
  absl::Barrier duplicate_barrier{2};
  handle->duplicate([&](absl::StatusOr<AsyncFileHandle> status) {
    duplicate_status = status;
    duplicate_barrier.Block();
  });
  duplicate_barrier.Block();
  ASSERT_EQ(absl::OkStatus(), duplicate_status.status());
  AsyncFileHandle dup_file = std::move(duplicate_status.value());
  // Close the original file.
  close(handle);
  absl::StatusOr<size_t> write_status;
  absl::Barrier write_barrier{2};
  Envoy::Buffer::OwnedImpl buf("hello");
  dup_file->write(buf, 0, [&](absl::StatusOr<size_t> result) {
    write_status = result;
    write_barrier.Block();
  });
  write_barrier.Block();
  // writing to the duplicate file should still work.
  ASSERT_EQ(absl::OkStatus(), write_status.status());
  EXPECT_EQ(5, write_status.value());
  close(dup_file);
}

TEST_F(AsyncFileHandleWithMockPosixTest, PartialReadReturnsPartialResult) {
  auto handle = createAnonymousFile();
  absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> read_status;
  absl::Barrier read_barrier{2};
  EXPECT_CALL(mock_posix_file_operations_, pread(_, _, _, _))
      .WillOnce([](int, void* buf, size_t, off_t) {
        memcpy(buf, "hel", 3);
        return 3;
      });
  handle->read(0, 5, [&](absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> status) {
    read_status = std::move(status.value());
    read_barrier.Block();
  });
  read_barrier.Block();
  ASSERT_EQ(absl::OkStatus(), read_status.status());
  EXPECT_EQ(std::string("hel"), read_status.value()->toString());
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
  absl::StatusOr<size_t> write_status;
  absl::Barrier write_barrier{2};
  Envoy::Buffer::OwnedImpl write_value{"hello"};
  EXPECT_CALL(mock_posix_file_operations_, pwrite(_, IsMemoryMatching("hello"), 5, 0))
      .WillOnce(Return(3));
  EXPECT_CALL(mock_posix_file_operations_, pwrite(_, IsMemoryMatching("lo"), 2, 3))
      .WillOnce(Return(2));
  handle->write(write_value, 0, [&](absl::StatusOr<size_t> status) {
    write_status = std::move(status.value());
    write_barrier.Block();
  });
  write_barrier.Block();
  ASSERT_EQ(absl::OkStatus(), write_status.status());
  EXPECT_EQ(5U, write_status.value());
  close(handle);
}

TEST_F(AsyncFileHandleWithMockPosixTest, CancellingDuplicateInProgressClosesTheFile) {
  auto handle = createAnonymousFile();
  absl::Barrier entering_dup{2}, finishing_dup{2};
  EXPECT_CALL(mock_posix_file_operations_, dup(_)).WillOnce([&]() {
    entering_dup.Block();
    finishing_dup.Block();
    return 4242;
  });
  auto cancel_dup = handle->duplicate([](absl::StatusOr<AsyncFileHandle>) {
    // Callback is not called if we cancel (already validated in manager tests)
    // so this is unimportant.
  });
  entering_dup.Block();
  cancel_dup();
  absl::Barrier closing{2};
  EXPECT_CALL(mock_posix_file_operations_, close(4242)).WillOnce([&]() {
    closing.Block();
    return 0;
  });
  finishing_dup.Block();
  closing.Block();
  close(handle);
}

TEST_F(AsyncFileHandleWithMockPosixTest, CancellingCreateHardLinkInProgressRemovesTheLink) {
  auto handle = createAnonymousFile();
  absl::Barrier entering_hardlink{2}, finishing_hardlink{2};
  std::string filename = "irrelevant_filename";
  EXPECT_CALL(mock_posix_file_operations_, linkat(_, _, _, Eq(filename), _)).WillOnce([&]() {
    entering_hardlink.Block();
    finishing_hardlink.Block();
    return 0;
  });
  auto cancel_hardlink = handle->createHardLink(filename, [](absl::Status) {
    // Callback is not called if we cancel, so this is unimportant.
  });
  entering_hardlink.Block();
  cancel_hardlink();
  absl::Barrier unlinking{2};
  EXPECT_CALL(mock_posix_file_operations_, unlink(Eq(filename))).WillOnce([&]() {
    unlinking.Block();
    return 0;
  });
  finishing_hardlink.Block();
  unlinking.Block();
  close(handle);
}

TEST_F(AsyncFileHandleWithMockPosixTest, CancellingFailedCreateHardLinkInProgressDoesNotUnlink) {
  auto handle = createAnonymousFile();
  absl::Barrier entering_hardlink{2}, finishing_hardlink{2};
  std::string filename = "irrelevant_filename";
  EXPECT_CALL(mock_posix_file_operations_, linkat(_, _, _, Eq(filename), _)).WillOnce([&]() {
    entering_hardlink.Block();
    finishing_hardlink.Block();
    errno = EBADF;
    return -1;
  });
  auto cancel_hardlink = handle->createHardLink(filename, [](absl::Status) {
    // Callback is not called if we cancel, so this is unimportant.
  });
  entering_hardlink.Block();
  cancel_hardlink();
  EXPECT_CALL(mock_posix_file_operations_, unlink(_)).Times(0);
  finishing_hardlink.Block();
  close(handle);
}

TEST_F(AsyncFileHandleWithMockPosixTest, CloseFailureReportsError) {
  auto handle = createAnonymousFile();
  absl::Status close_status;
  absl::Barrier close_barrier{2};
  EXPECT_CALL(mock_posix_file_operations_, close(1))
      .WillOnce([]() {
        errno = EBADF;
        return -1;
      })
      .WillOnce(Return(0));
  handle->close([&](absl::Status status) {
    close_status = status;
    close_barrier.Block();
  });
  close_barrier.Block();
  EXPECT_EQ(absl::StatusCode::kFailedPrecondition, close_status.code()) << close_status;
  close(handle);
}

TEST_F(AsyncFileHandleWithMockPosixTest, DuplicateFailureReportsError) {
  auto handle = createAnonymousFile();
  absl::StatusOr<AsyncFileHandle> dup_status;
  absl::Barrier dup_barrier{2};
  EXPECT_CALL(mock_posix_file_operations_, dup(_)).WillOnce([]() {
    errno = EBADF;
    return -1;
  });
  handle->duplicate([&](absl::StatusOr<AsyncFileHandle> status) {
    dup_status = status;
    dup_barrier.Block();
  });
  dup_barrier.Block();
  EXPECT_EQ(absl::StatusCode::kFailedPrecondition, dup_status.status().code())
      << dup_status.status();
  close(handle);
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
