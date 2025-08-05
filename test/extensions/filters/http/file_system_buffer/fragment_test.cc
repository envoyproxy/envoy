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
using ::testing::MockFunction;
using ::testing::StrictMock;

class FileSystemBufferFilterFragmentTest : public ::testing::Test {
public:
  void resolveFileActions() { dispatcher_->run(Event::Dispatcher::RunType::Block); }

protected:
  MockAsyncFileHandle handle_ = std::make_shared<StrictMock<MockAsyncFileContext>>();

  void moveFragmentToStorage(Fragment* frag) {
    EXPECT_CALL(*handle_, write(_, _, _, _))
        .WillOnce([frag](Event::Dispatcher* dispatcher, Buffer::Instance&, off_t,
                         absl::AnyInvocable<void(absl::StatusOr<size_t>)> callback) {
          dispatcher->post([frag, callback = std::move(callback)]() mutable {
            std::move(callback)(frag->size());
          });
          return []() {};
        });
    MockFunction<void(absl::Status)> callback;
    EXPECT_OK(frag->toStorage(handle_, 123, *dispatcher_, callback.AsStdFunction()));
    EXPECT_CALL(callback, Call(absl::OkStatus()));
    resolveFileActions();
  }

  Api::ApiPtr api_ = Api::createApiForTest();
  Event::DispatcherPtr dispatcher_ = api_->allocateDispatcher("test_thread");
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
  EXPECT_CALL(*handle_, write(_, BufferStringEqual("hello"), 123, _))
      .WillOnce([](Event::Dispatcher* dispatcher, Buffer::Instance&, off_t,
                   absl::AnyInvocable<void(absl::StatusOr<size_t>)> callback) {
        dispatcher->post([callback = std::move(callback)]() mutable { std::move(callback)(5); });
        return []() {};
      });
  // Request the fragment be moved to storage.
  MockFunction<void(absl::Status)> write_callback;
  EXPECT_OK(frag.toStorage(handle_, 123, *dispatcher_, write_callback.AsStdFunction()));
  // Before the file confirms written, the state should be neither in memory nor in storage.
  EXPECT_FALSE(frag.isMemory());
  EXPECT_FALSE(frag.isStorage());
  // Fake the file thread confirming 5 bytes were written.
  EXPECT_CALL(write_callback, Call(absl::OkStatus()));
  resolveFileActions();
  // Now the fragment should be tagged as being in storage.
  EXPECT_TRUE(frag.isStorage());
  EXPECT_FALSE(frag.isMemory());
  EXPECT_CALL(*handle_, read(_, 123, 5, _))
      .WillOnce(
          [](Event::Dispatcher* dispatcher, off_t, size_t,
             absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<Buffer::Instance>>)> callback) {
            dispatcher->post([callback = std::move(callback)]() mutable {
              std::move(callback)(std::make_unique<Buffer::OwnedImpl>("hello"));
            });
            return []() {};
          });
  // Request the fragment be moved from storage.
  MockFunction<void(absl::Status)> read_callback;
  EXPECT_OK(frag.fromStorage(handle_, *dispatcher_, read_callback.AsStdFunction()));
  // Before the file confirms read, the state should be neither in memory nor storage.
  EXPECT_FALSE(frag.isMemory());
  EXPECT_FALSE(frag.isStorage());
  // Fake the file thread completing read.
  EXPECT_CALL(read_callback, Call(absl::OkStatus()));
  resolveFileActions();
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
  EXPECT_CALL(*handle_, write(_, BufferStringEqual("hello"), 123, _))
      .WillOnce([](Event::Dispatcher* dispatcher, Buffer::Instance&, off_t,
                   absl::AnyInvocable<void(absl::StatusOr<size_t>)> callback) {
        dispatcher->post([callback = std::move(callback)]() mutable {
          std::move(callback)(absl::UnknownError("write error"));
        });
        return []() {};
      });
  // Request the fragment be moved to storage.
  MockFunction<void(absl::Status)> callback;
  EXPECT_OK(frag.toStorage(handle_, 123, *dispatcher_, callback.AsStdFunction()));
  EXPECT_CALL(callback, Call(Eq(absl::UnknownError("write error"))));
  resolveFileActions();
}

TEST_F(FileSystemBufferFilterFragmentTest, ReturnsErrorOnWriteIncomplete) {
  Buffer::OwnedImpl input("hello");
  Fragment frag(input);
  EXPECT_CALL(*handle_, write(_, BufferStringEqual("hello"), 123, _))
      .WillOnce([](Event::Dispatcher* dispatcher, Buffer::Instance&, off_t,
                   absl::AnyInvocable<void(absl::StatusOr<size_t>)> callback) {
        dispatcher->post([callback = std::move(callback)]() mutable { std::move(callback)(2); });
        return []() {};
      });
  // Request the fragment be moved to storage.
  MockFunction<void(absl::Status)> callback;
  EXPECT_OK(frag.toStorage(handle_, 123, *dispatcher_, callback.AsStdFunction()));
  // Fake file says it wrote 2 bytes when the fragment was of size 5 - this should
  // provoke the expected error in the callback above.
  EXPECT_CALL(callback, Call(HasStatusMessage(HasSubstr("wrote 2 bytes, wanted 5"))));
  resolveFileActions();
}

TEST_F(FileSystemBufferFilterFragmentTest, ReturnsErrorOnReadError) {
  Buffer::OwnedImpl input("hello");
  Fragment frag(input);
  moveFragmentToStorage(&frag);
  EXPECT_CALL(*handle_, read(_, 123, 5, _))
      .WillOnce(
          [](Event::Dispatcher* dispatcher, off_t, size_t,
             absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<Buffer::Instance>>)> callback) {
            dispatcher->post([callback = std::move(callback)]() mutable {
              std::move(callback)(absl::UnknownError("read error"));
            });
            return []() {};
          });
  // Request the fragment be moved from storage.
  MockFunction<void(absl::Status)> callback;
  EXPECT_OK(frag.fromStorage(handle_, *dispatcher_, callback.AsStdFunction()));
  EXPECT_CALL(callback, Call(Eq(absl::UnknownError("read error"))));
  resolveFileActions();
}

TEST_F(FileSystemBufferFilterFragmentTest, ReturnsErrorOnReadIncomplete) {
  Buffer::OwnedImpl input("hello");
  Fragment frag(input);
  moveFragmentToStorage(&frag);
  absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<Buffer::Instance>>)>
      captured_read_callback;
  EXPECT_CALL(*handle_, read(_, 123, 5, _))
      .WillOnce(
          [](Event::Dispatcher* dispatcher, off_t, size_t,
             absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<Buffer::Instance>>)> callback) {
            dispatcher->post([callback = std::move(callback)]() mutable {
              std::move(callback)(std::make_unique<Buffer::OwnedImpl>("he"));
            });
            return []() {};
          });
  MockFunction<void(absl::Status)> callback;
  // Request the fragment be moved from storage.
  EXPECT_OK(frag.fromStorage(handle_, *dispatcher_, callback.AsStdFunction()));
  // Mock file system declares a read error. This should
  // provoke the expected error in the callback above.
  EXPECT_CALL(callback, Call(HasStatusMessage(HasSubstr("read got 2 bytes, wanted 5"))));
  resolveFileActions();
}

TEST_F(FileSystemBufferFilterFragmentTest, ToStorageFailsWhenNotInMemoryState) {
  // Create a fragment that's already in storage state.
  Buffer::OwnedImpl input("hello");
  Fragment frag(input);
  moveFragmentToStorage(&frag);
  EXPECT_TRUE(frag.isStorage());

  // Attempting toStorage() on a storage fragment should fail gracefully.
  MockFunction<void(absl::Status)> callback;
  auto result = frag.toStorage(handle_, 123, *dispatcher_, callback.AsStdFunction());
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(result.status().message(), HasSubstr("not in the memory state"));
}

TEST_F(FileSystemBufferFilterFragmentTest, FromStorageFailsWhenNotInStorageState) {
  // Create a fragment in memory state.
  Buffer::OwnedImpl input("hello");
  Fragment frag(input);
  EXPECT_TRUE(frag.isMemory());

  // Attempting fromStorage() on a memory fragment should fail gracefully.
  MockFunction<void(absl::Status)> callback;
  auto result = frag.fromStorage(handle_, *dispatcher_, callback.AsStdFunction());
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(result.status().message(), HasSubstr("not in the storage state"));
}

TEST_F(FileSystemBufferFilterFragmentTest, ToStorageFailsWhenInTransitionState) {
  // Create a fragment and start moving it to storage but don't complete the operation.
  Buffer::OwnedImpl input("hello");
  Fragment frag(input);

  // Start the toStorage operation. This puts fragment in "writing" state.
  EXPECT_CALL(*handle_, write(_, _, _, _))
      .WillOnce([](Event::Dispatcher*, Buffer::Instance&, off_t,
                   absl::AnyInvocable<void(absl::StatusOr<size_t>)>) {
        // Don't call the callback and leave the fragment in transition state.
        return []() {};
      });

  MockFunction<void(absl::Status)> write_callback;
  EXPECT_OK(frag.toStorage(handle_, 123, *dispatcher_, write_callback.AsStdFunction()));
  // Fragment is now in transition state - neither memory nor storage
  EXPECT_FALSE(frag.isMemory());
  EXPECT_FALSE(frag.isStorage());

  // Attempting another toStorage() should fail.
  MockFunction<void(absl::Status)> second_callback;
  auto result = frag.toStorage(handle_, 456, *dispatcher_, second_callback.AsStdFunction());
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(result.status().message(), HasSubstr("not in the memory state"));
}

} // namespace FileSystemBuffer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
