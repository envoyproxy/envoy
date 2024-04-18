#include <functional>
#include <memory>
#include <vector>

#include "source/extensions/filters/http/file_system_buffer/fragment.h"

#include "test/extensions/common/async_files/mocks.h"
#include "test/mocks/buffer/mocks.h"
#include "test/test_common/status_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileSystemBuffer {

using Extensions::Common::AsyncFiles::CancelFunction;
using Extensions::Common::AsyncFiles::MockAsyncFileContext;
using Extensions::Common::AsyncFiles::MockAsyncFileHandle;
using StatusHelpers::HasStatusMessage;
using ::testing::_;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::StrictMock;

std::function<void(absl::Status)> storageSuccessCallback() {
  return [](absl::Status status) { ASSERT_OK(status); };
}

template <typename MatcherT>
std::function<void(absl::Status)> storageFailureCallback(MatcherT matcher) {
  return [matcher](absl::Status status) { EXPECT_THAT(status, matcher); };
}

void dispatchImmediately(std::function<void()> callback) { callback(); }

class FileSystemBufferFilterFragmentTest : public ::testing::Test {
public:
protected:
  MockAsyncFileHandle handle_ = std::make_shared<StrictMock<MockAsyncFileContext>>();

  void moveFragmentToStorage(Fragment* frag) {
    EXPECT_CALL(*handle_, write(_, _, _))
        .WillOnce(
            [frag](Buffer::Instance&, off_t, std::function<void(absl::StatusOr<size_t>)> callback) {
              callback(frag->size());
              return []() {};
            });
    EXPECT_OK(frag->toStorage(handle_, 123, &dispatchImmediately, storageSuccessCallback()));
  }
};

TEST_F(FileSystemBufferFilterFragmentTest, CreatesAndExtractsWithoutCopying) {
  Buffer::OwnedImpl input("hello");
  void* original_address = input.frontSlice().mem_;
  Fragment frag(input);
  EXPECT_TRUE(frag.isMemory());
  EXPECT_FALSE(frag.isStorage());
  EXPECT_EQ(frag.size(), 5);
  auto out = frag.extract();
  EXPECT_EQ(out->toString(), "hello");
  EXPECT_EQ(out->frontSlice().mem_, original_address);
}

TEST_F(FileSystemBufferFilterFragmentTest, CreatesFragmentFromPartialBufferAndConsumes) {
  Buffer::OwnedImpl input("hello");
  Fragment frag(input, 3);
  EXPECT_TRUE(frag.isMemory());
  EXPECT_FALSE(frag.isStorage());
  EXPECT_EQ(frag.size(), 3);
  auto out = frag.extract();
  EXPECT_EQ(out->toString(), "hel");
  EXPECT_EQ(input.toString(), "lo");
}

TEST_F(FileSystemBufferFilterFragmentTest, WritesAndReadsBack) {
  Buffer::OwnedImpl input("hello");
  Fragment frag(input);
  std::function<void(absl::StatusOr<size_t>)> captured_write_callback;
  EXPECT_CALL(*handle_, write(BufferStringEqual("hello"), 123, _))
      .WillOnce([&captured_write_callback](Buffer::Instance&, off_t,
                                           std::function<void(absl::StatusOr<size_t>)> callback) {
        captured_write_callback = std::move(callback);
        return []() {};
      });
  // Request the fragment be moved to storage.
  EXPECT_OK(frag.toStorage(handle_, 123, &dispatchImmediately, storageSuccessCallback()));
  // Before the file confirms written, the state should be neither in memory nor in storage.
  EXPECT_FALSE(frag.isMemory());
  EXPECT_FALSE(frag.isStorage());
  // Fake the file thread confirming 5 bytes were written.
  captured_write_callback(5);
  // Now the fragment should be tagged as being in storage.
  EXPECT_TRUE(frag.isStorage());
  EXPECT_FALSE(frag.isMemory());
  std::function<void(absl::StatusOr<std::unique_ptr<Buffer::Instance>>)> captured_read_callback;
  EXPECT_CALL(*handle_, read(123, 5, _))
      .WillOnce(
          [&captured_read_callback](
              off_t, size_t,
              std::function<void(absl::StatusOr<std::unique_ptr<Buffer::Instance>>)> callback) {
            captured_read_callback = std::move(callback);
            return []() {};
          });
  // Request the fragment be moved from storage.
  EXPECT_OK(frag.fromStorage(handle_, &dispatchImmediately, storageSuccessCallback()));
  // Before the file confirms read, the state should be neither in memory nor storage.
  EXPECT_FALSE(frag.isMemory());
  EXPECT_FALSE(frag.isStorage());
  // Fake the file thread completing read.
  captured_read_callback(std::make_unique<Buffer::OwnedImpl>("hello"));
  // Now the fragment should be tagged as being in memory.
  EXPECT_TRUE(frag.isMemory());
  EXPECT_FALSE(frag.isStorage());
  // The data extracted from the fragment should be the same as what was read.
  auto out = frag.extract();
  EXPECT_EQ(out->toString(), "hello");
}

TEST_F(FileSystemBufferFilterFragmentTest, ReturnsErrorOnWriteError) {
  Buffer::OwnedImpl input("hello");
  Fragment frag(input);
  auto write_error = absl::UnknownError("write error");
  std::function<void(absl::StatusOr<size_t>)> captured_write_callback;
  EXPECT_CALL(*handle_, write(BufferStringEqual("hello"), 123, _))
      .WillOnce([&captured_write_callback](Buffer::Instance&, off_t,
                                           std::function<void(absl::StatusOr<size_t>)> callback) {
        captured_write_callback = std::move(callback);
        return []() {};
      });
  // Request the fragment be moved to storage.
  EXPECT_OK(
      frag.toStorage(handle_, 123, &dispatchImmediately, storageFailureCallback(Eq(write_error))));

  // Fake file system declares a write error. This should
  // provoke the expected error in the callback above.
  captured_write_callback(write_error);
}

TEST_F(FileSystemBufferFilterFragmentTest, ReturnsErrorOnWriteIncomplete) {
  Buffer::OwnedImpl input("hello");
  Fragment frag(input);
  std::function<void(absl::StatusOr<size_t>)> captured_write_callback;
  EXPECT_CALL(*handle_, write(BufferStringEqual("hello"), 123, _))
      .WillOnce([&captured_write_callback](Buffer::Instance&, off_t,
                                           std::function<void(absl::StatusOr<size_t>)> callback) {
        captured_write_callback = std::move(callback);
        return []() {};
      });
  // Request the fragment be moved to storage.
  EXPECT_OK(frag.toStorage(
      handle_, 123, &dispatchImmediately,
      storageFailureCallback(HasStatusMessage(HasSubstr("wrote 2 bytes, wanted 5")))));

  // Fake file says it wrote 2 bytes when the fragment was of size 5 - this should
  // provoke the expected error in the callback above.
  captured_write_callback(2);
}

TEST_F(FileSystemBufferFilterFragmentTest, ReturnsErrorOnReadError) {
  Buffer::OwnedImpl input("hello");
  Fragment frag(input);
  moveFragmentToStorage(&frag);
  auto read_error = absl::UnknownError("read error");
  std::function<void(absl::StatusOr<std::unique_ptr<Buffer::Instance>>)> captured_read_callback;
  EXPECT_CALL(*handle_, read(123, 5, _))
      .WillOnce(
          [&captured_read_callback](
              off_t, size_t,
              std::function<void(absl::StatusOr<std::unique_ptr<Buffer::Instance>>)> callback) {
            captured_read_callback = std::move(callback);
            return []() {};
          });
  // Request the fragment be moved from storage.
  EXPECT_OK(
      frag.fromStorage(handle_, &dispatchImmediately, storageFailureCallback(Eq(read_error))));
  // Fake file system declares a read error. This should
  // provoke the expected error in the callback above.
  captured_read_callback(read_error);
}

TEST_F(FileSystemBufferFilterFragmentTest, ReturnsErrorOnReadIncomplete) {
  Buffer::OwnedImpl input("hello");
  Fragment frag(input);
  moveFragmentToStorage(&frag);
  std::function<void(absl::StatusOr<std::unique_ptr<Buffer::Instance>>)> captured_read_callback;
  EXPECT_CALL(*handle_, read(123, 5, _))
      .WillOnce(
          [&captured_read_callback](
              off_t, size_t,
              std::function<void(absl::StatusOr<std::unique_ptr<Buffer::Instance>>)> callback) {
            captured_read_callback = std::move(callback);
            return []() {};
          });
  // Request the fragment be moved from storage.
  EXPECT_OK(frag.fromStorage(
      handle_, &dispatchImmediately,
      storageFailureCallback(HasStatusMessage(HasSubstr("read got 2 bytes, wanted 5")))));

  // Fake file system declares a read error. This should
  // provoke the expected error in the callback above.
  captured_read_callback(std::make_unique<Buffer::OwnedImpl>("he"));
}

} // namespace FileSystemBuffer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
