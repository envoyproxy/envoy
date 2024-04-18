#pragma once

#include "source/extensions/common/async_files/async_file_handle.h"
#include "source/extensions/common/async_files/async_file_manager.h"
#include "source/extensions/common/async_files/async_file_manager_factory.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

class MockAsyncFileManager;

class MockAsyncFileContext : public Extensions::Common::AsyncFiles::AsyncFileContext {
public:
  MockAsyncFileContext() = default;
  explicit MockAsyncFileContext(std::shared_ptr<MockAsyncFileManager> manager);
  // The default behavior of the methods that would enqueue an action is to enqueue an
  // appropriate MockAsyncFileAction on the MockAsyncFileManager (if one was provided).
  // These can be consumed by calling MockAsyncFileManager::nextActionCompletes
  // with the desired parameter to the on_complete callback.
  MOCK_METHOD(absl::StatusOr<CancelFunction>, stat,
              (std::function<void(absl::StatusOr<struct stat>)> on_complete));
  MOCK_METHOD(absl::StatusOr<CancelFunction>, createHardLink,
              (absl::string_view filename, std::function<void(absl::Status)> on_complete));
  MOCK_METHOD(absl::Status, close, (std::function<void(absl::Status)> on_complete));
  MOCK_METHOD(absl::StatusOr<CancelFunction>, read,
              (off_t offset, size_t length,
               std::function<void(absl::StatusOr<Buffer::InstancePtr>)> on_complete));
  MOCK_METHOD(absl::StatusOr<CancelFunction>, write,
              (Buffer::Instance & contents, off_t offset,
               std::function<void(absl::StatusOr<size_t>)> on_complete));
  MOCK_METHOD(absl::StatusOr<CancelFunction>, duplicate,
              (std::function<void(absl::StatusOr<std::shared_ptr<AsyncFileContext>>)> on_complete));

private:
  std::shared_ptr<MockAsyncFileManager> manager_;
};

using MockAsyncFileHandle = std::shared_ptr<MockAsyncFileContext>;

class MockAsyncFileAction : public AsyncFileAction {
public:
  virtual std::string describe() const PURE;
  void execute() override{};
};

template <typename T> class TypedMockAsyncFileAction : public MockAsyncFileAction {
public:
  explicit TypedMockAsyncFileAction(T on_complete) : on_complete_(on_complete) {}
  T on_complete_;
  std::string describe() const override { return typeid(T).name(); }
};

class MockAsyncFileManager : public Extensions::Common::AsyncFiles::AsyncFileManager {
public:
  MockAsyncFileManager();
  // The default behavior of the methods that would enqueue an action is to enqueue a mock action.
  MOCK_METHOD(CancelFunction, createAnonymousFile,
              (absl::string_view path,
               std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete));
  MOCK_METHOD(CancelFunction, openExistingFile,
              (absl::string_view filename, Mode mode,
               std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete));
  MOCK_METHOD(CancelFunction, stat,
              (absl::string_view filename,
               std::function<void(absl::StatusOr<struct stat>)> on_complete));
  MOCK_METHOD(CancelFunction, unlink,
              (absl::string_view filename, std::function<void(absl::Status)> on_complete));
  MOCK_METHOD(std::string, describe, (), (const));

  // mockCancel is called any time any action queued by mock is cancelled. It isn't overriding
  // a function from the real class, it's just used for verification.
  MOCK_METHOD(void, mockCancel, ());

  // With a non-overridden MockAsyncFileManager, action functions put the action into queue_,
  // and the actions can be completed by calling nextActionCompletes with the parameter to
  // be passed to the callback.
  // For callbacks taking StatusOr<T>, the value must be passed as explicitly a StatusOr<T>,
  // not just a Status or a T.
  template <typename T> void nextActionCompletes(T result) {
    ASSERT_FALSE(queue_.empty());
    auto action =
        std::dynamic_pointer_cast<TypedMockAsyncFileAction<std::function<void(T)>>>(queue_.front());
    ASSERT_TRUE(action.get() != nullptr)
        << "mismatched type for nextActionCompletes: action is " << queue_.front()->describe()
        << ", nextActionCompletes was given " << typeid(T).name();
    queue_.pop_front();
    action->on_complete_(std::move(result));
  }

  std::deque<std::shared_ptr<MockAsyncFileAction>> queue_;

private:
  MOCK_METHOD(CancelFunction, enqueue, (const std::shared_ptr<AsyncFileAction> action));
  friend class MockAsyncFileContext;
};

class MockAsyncFileManagerFactory : public Extensions::Common::AsyncFiles::AsyncFileManagerFactory {
public:
  MOCK_METHOD(std::shared_ptr<AsyncFileManager>, getAsyncFileManager,
              (const envoy::extensions::common::async_files::v3::AsyncFileManagerConfig& config,
               Api::OsSysCalls* substitute_posix_file_operations));
};

// Add deduction guides for comping with the ctad-maybe-unsupported warning
TypedMockAsyncFileAction(std::function<void(absl::Status)>)
    ->TypedMockAsyncFileAction<std::function<void(absl::Status)>>;
TypedMockAsyncFileAction(std::function<void(absl::StatusOr<Buffer::InstancePtr>)>)
    ->TypedMockAsyncFileAction<std::function<void(absl::StatusOr<Buffer::InstancePtr>)>>;
TypedMockAsyncFileAction(std::function<void(absl::StatusOr<size_t>)>)
    ->TypedMockAsyncFileAction<std::function<void(absl::StatusOr<size_t>)>>;
TypedMockAsyncFileAction(std::function<void(absl::StatusOr<std::shared_ptr<AsyncFileContext>>)>)
    ->TypedMockAsyncFileAction<
        std::function<void(absl::StatusOr<std::shared_ptr<AsyncFileContext>>)>>;

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
