#pragma once

#include <queue>

#include "source/extensions/common/async_files/async_file_handle.h"
#include "source/extensions/common/async_files/async_file_manager.h"
#include "source/extensions/common/async_files/async_file_manager_factory.h"
#include "source/extensions/common/async_files/async_file_manager_thread_pool.h"

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
              (Event::Dispatcher * dispatcher,
               absl::AnyInvocable<void(absl::StatusOr<struct stat>)> on_complete));
  MOCK_METHOD(absl::StatusOr<CancelFunction>, createHardLink,
              (Event::Dispatcher * dispatcher, absl::string_view filename,
               absl::AnyInvocable<void(absl::Status)> on_complete));
  MOCK_METHOD(absl::StatusOr<CancelFunction>, close,
              (Event::Dispatcher * dispatcher, absl::AnyInvocable<void(absl::Status)> on_complete));
  MOCK_METHOD(absl::StatusOr<CancelFunction>, read,
              (Event::Dispatcher * dispatcher, off_t offset, size_t length,
               absl::AnyInvocable<void(absl::StatusOr<Buffer::InstancePtr>)> on_complete));
  MOCK_METHOD(absl::StatusOr<CancelFunction>, write,
              (Event::Dispatcher * dispatcher, Buffer::Instance& contents, off_t offset,
               absl::AnyInvocable<void(absl::StatusOr<size_t>)> on_complete));
  MOCK_METHOD(
      absl::StatusOr<CancelFunction>, duplicate,
      (Event::Dispatcher * dispatcher,
       absl::AnyInvocable<void(absl::StatusOr<std::shared_ptr<AsyncFileContext>>)> on_complete));

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
  explicit TypedMockAsyncFileAction(T on_complete) : on_complete_(std::move(on_complete)) {}
  void onComplete() override {}
  T on_complete_;
  std::string describe() const override { return typeid(T).name(); }
};

class MockAsyncFileManager : public Extensions::Common::AsyncFiles::AsyncFileManager {
public:
  MockAsyncFileManager();
  // The default behavior of the methods that would enqueue an action is to enqueue a mock action.
  MOCK_METHOD(CancelFunction, createAnonymousFile,
              (Event::Dispatcher * dispatcher, absl::string_view path,
               absl::AnyInvocable<void(absl::StatusOr<AsyncFileHandle>)> on_complete));
  MOCK_METHOD(CancelFunction, openExistingFile,
              (Event::Dispatcher * dispatcher, absl::string_view filename, Mode mode,
               absl::AnyInvocable<void(absl::StatusOr<AsyncFileHandle>)> on_complete));
  MOCK_METHOD(CancelFunction, stat,
              (Event::Dispatcher * dispatcher, absl::string_view filename,
               absl::AnyInvocable<void(absl::StatusOr<struct stat>)> on_complete));
  MOCK_METHOD(CancelFunction, unlink,
              (Event::Dispatcher * dispatcher, absl::string_view filename,
               absl::AnyInvocable<void(absl::Status)> on_complete));
  MOCK_METHOD(std::string, describe, (), (const));
  MOCK_METHOD(void, waitForIdle, ());
  MOCK_METHOD(void, postCancelledActionForCleanup, (std::unique_ptr<AsyncFileAction> action));

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
    auto entry = std::move(queue_.front());
    queue_.pop();
    auto action =
        dynamic_cast<TypedMockAsyncFileAction<absl::AnyInvocable<void(T)>>*>(entry.action_.get());
    ASSERT_TRUE(action != nullptr)
        << "mismatched type for nextActionCompletes: action is " << action->describe()
        << ", nextActionCompletes was given " << typeid(T).name();
    if (entry.dispatcher_) {
      entry.dispatcher_->post([action = std::move(entry.action_), state = std::move(entry.state_),
                               result = std::move(result)]() mutable {
        if (state->load() != QueuedAction::State::Cancelled) {
          dynamic_cast<TypedMockAsyncFileAction<absl::AnyInvocable<void(T)>>*>(action.get())
              ->on_complete_(std::move(result));
        }
      });
    }
  }

  std::queue<QueuedAction> queue_;

private:
  MOCK_METHOD(CancelFunction, enqueue,
              (Event::Dispatcher * dispatcher, std::unique_ptr<AsyncFileAction> action));
  friend class MockAsyncFileContext;
};

class MockAsyncFileManagerFactory : public Extensions::Common::AsyncFiles::AsyncFileManagerFactory {
public:
  MOCK_METHOD(std::shared_ptr<AsyncFileManager>, getAsyncFileManager,
              (const envoy::extensions::common::async_files::v3::AsyncFileManagerConfig& config,
               Api::OsSysCalls* substitute_posix_file_operations));
};

// Add deduction guides for comping with the ctad-maybe-unsupported warning
TypedMockAsyncFileAction(absl::AnyInvocable<void(absl::Status)>)
    ->TypedMockAsyncFileAction<absl::AnyInvocable<void(absl::Status)>>;
TypedMockAsyncFileAction(absl::AnyInvocable<void(absl::StatusOr<Buffer::InstancePtr>)>)
    ->TypedMockAsyncFileAction<absl::AnyInvocable<void(absl::StatusOr<Buffer::InstancePtr>)>>;
TypedMockAsyncFileAction(absl::AnyInvocable<void(absl::StatusOr<size_t>)>)
    ->TypedMockAsyncFileAction<absl::AnyInvocable<void(absl::StatusOr<size_t>)>>;
TypedMockAsyncFileAction(
    absl::AnyInvocable<void(absl::StatusOr<std::shared_ptr<AsyncFileContext>>)>)
    ->TypedMockAsyncFileAction<
        absl::AnyInvocable<void(absl::StatusOr<std::shared_ptr<AsyncFileContext>>)>>;

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
