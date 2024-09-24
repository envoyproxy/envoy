#include "test/extensions/common/async_files/mocks.h"

#include <memory>

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

using ::testing::_;

MockAsyncFileContext::MockAsyncFileContext(std::shared_ptr<MockAsyncFileManager> manager)
    : manager_(manager) {
  ON_CALL(*this, stat(_, _))
      .WillByDefault([this](Event::Dispatcher* dispatcher,
                            absl::AnyInvocable<void(absl::StatusOr<struct stat>)> on_complete) {
        return manager_->enqueue(dispatcher,
                                 std::unique_ptr<MockAsyncFileAction>(
                                     new TypedMockAsyncFileAction(std::move(on_complete))));
      });
  ON_CALL(*this, createHardLink(_, _, _))
      .WillByDefault([this](Event::Dispatcher* dispatcher, absl::string_view,
                            absl::AnyInvocable<void(absl::Status)> on_complete) {
        return manager_->enqueue(dispatcher,
                                 std::unique_ptr<MockAsyncFileAction>(
                                     new TypedMockAsyncFileAction(std::move(on_complete))));
      });
  EXPECT_CALL(*this, close(_, _))
      .WillOnce([this](Event::Dispatcher* dispatcher,
                       absl::AnyInvocable<void(absl::Status)> on_complete) mutable {
        return manager_->enqueue(dispatcher,
                                 std::unique_ptr<MockAsyncFileAction>(
                                     new TypedMockAsyncFileAction(std::move(on_complete))));
      });
  ON_CALL(*this, read(_, _, _, _))
      .WillByDefault(
          [this](Event::Dispatcher* dispatcher, off_t, size_t,
                 absl::AnyInvocable<void(absl::StatusOr<Buffer::InstancePtr>)> on_complete) {
            return manager_->enqueue(dispatcher,
                                     std::unique_ptr<MockAsyncFileAction>(
                                         new TypedMockAsyncFileAction(std::move(on_complete))));
          });
  ON_CALL(*this, write(_, _, _, _))
      .WillByDefault([this](Event::Dispatcher* dispatcher, Buffer::Instance&, off_t,
                            absl::AnyInvocable<void(absl::StatusOr<size_t>)> on_complete) {
        return manager_->enqueue(dispatcher,
                                 std::unique_ptr<MockAsyncFileAction>(
                                     new TypedMockAsyncFileAction(std::move(on_complete))));
      });
  ON_CALL(*this, duplicate(_, _))
      .WillByDefault(
          [this](Event::Dispatcher* dispatcher,
                 absl::AnyInvocable<void(absl::StatusOr<std::shared_ptr<AsyncFileContext>>)>
                     on_complete) {
            return manager_->enqueue(dispatcher,
                                     std::unique_ptr<MockAsyncFileAction>(
                                         new TypedMockAsyncFileAction(std::move(on_complete))));
          });
};

MockAsyncFileManager::MockAsyncFileManager() {
  ON_CALL(*this, enqueue(_, _))
      .WillByDefault(
          [this](Event::Dispatcher* dispatcher, std::unique_ptr<AsyncFileAction> action) {
            auto entry = QueuedAction{std::move(action), dispatcher};
            auto cancel_func = [this, state = entry.state_]() {
              state->store(QueuedAction::State::Cancelled);
              mockCancel();
            };
            queue_.push(std::move(entry));
            return cancel_func;
          });
  ON_CALL(*this, stat(_, _, _))
      .WillByDefault([this](Event::Dispatcher* dispatcher, absl::string_view,
                            absl::AnyInvocable<void(absl::StatusOr<struct stat>)> on_complete) {
        return enqueue(dispatcher, std::unique_ptr<MockAsyncFileAction>(
                                       new TypedMockAsyncFileAction(std::move(on_complete))));
      });
  ON_CALL(*this, createAnonymousFile(_, _, _))
      .WillByDefault([this](Event::Dispatcher* dispatcher, absl::string_view,
                            absl::AnyInvocable<void(absl::StatusOr<AsyncFileHandle>)> on_complete) {
        return enqueue(dispatcher, std::unique_ptr<MockAsyncFileAction>(
                                       new TypedMockAsyncFileAction(std::move(on_complete))));
      });
  ON_CALL(*this, openExistingFile(_, _, _, _))
      .WillByDefault([this](Event::Dispatcher* dispatcher, absl::string_view, Mode,
                            absl::AnyInvocable<void(absl::StatusOr<AsyncFileHandle>)> on_complete) {
        return enqueue(dispatcher, std::unique_ptr<MockAsyncFileAction>(
                                       new TypedMockAsyncFileAction(std::move(on_complete))));
      });
  ON_CALL(*this, unlink(_, _, _))
      .WillByDefault([this](Event::Dispatcher* dispatcher, absl::string_view,
                            absl::AnyInvocable<void(absl::Status)> on_complete) {
        return enqueue(dispatcher, std::unique_ptr<MockAsyncFileAction>(
                                       new TypedMockAsyncFileAction(std::move(on_complete))));
      });
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
