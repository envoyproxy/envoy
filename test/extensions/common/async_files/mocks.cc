#include "test/extensions/common/async_files/mocks.h"

#include <memory>

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

using ::testing::_;

MockAsyncFileContext::MockAsyncFileContext(std::shared_ptr<MockAsyncFileManager> manager)
    : manager_(manager) {
  ON_CALL(*this, stat(_))
      .WillByDefault([this](std::function<void(absl::StatusOr<struct stat>)> on_complete) {
        return manager_->enqueue(
            std::shared_ptr<MockAsyncFileAction>(new TypedMockAsyncFileAction(on_complete)));
      });
  ON_CALL(*this, createHardLink(_, _))
      .WillByDefault([this](absl::string_view, std::function<void(absl::Status)> on_complete) {
        return manager_->enqueue(
            std::shared_ptr<MockAsyncFileAction>(new TypedMockAsyncFileAction(on_complete)));
      });
  EXPECT_CALL(*this, close(_)).WillOnce([this](std::function<void(absl::Status)> on_complete) {
    manager_->enqueue(
        std::shared_ptr<MockAsyncFileAction>(new TypedMockAsyncFileAction(on_complete)));
    return absl::OkStatus();
  });
  ON_CALL(*this, read(_, _, _))
      .WillByDefault([this](off_t, size_t,
                            std::function<void(absl::StatusOr<Buffer::InstancePtr>)> on_complete) {
        return manager_->enqueue(
            std::shared_ptr<MockAsyncFileAction>(new TypedMockAsyncFileAction(on_complete)));
      });
  ON_CALL(*this, write(_, _, _))
      .WillByDefault([this](Buffer::Instance&, off_t,
                            std::function<void(absl::StatusOr<size_t>)> on_complete) {
        return manager_->enqueue(
            std::shared_ptr<MockAsyncFileAction>(new TypedMockAsyncFileAction(on_complete)));
      });
  ON_CALL(*this, duplicate(_))
      .WillByDefault(
          [this](
              std::function<void(absl::StatusOr<std::shared_ptr<AsyncFileContext>>)> on_complete) {
            return manager_->enqueue(
                std::shared_ptr<MockAsyncFileAction>(new TypedMockAsyncFileAction(on_complete)));
          });
};

MockAsyncFileManager::MockAsyncFileManager() {
  ON_CALL(*this, enqueue(_)).WillByDefault([this](const std::shared_ptr<AsyncFileAction> action) {
    queue_.push_back(std::dynamic_pointer_cast<MockAsyncFileAction>(action));
    return [this]() { mockCancel(); };
  });
  ON_CALL(*this, stat(_, _))
      .WillByDefault(
          [this](absl::string_view, std::function<void(absl::StatusOr<struct stat>)> on_complete) {
            return enqueue(
                std::shared_ptr<MockAsyncFileAction>(new TypedMockAsyncFileAction(on_complete)));
          });
  ON_CALL(*this, createAnonymousFile(_, _))
      .WillByDefault([this](absl::string_view,
                            std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete) {
        return enqueue(
            std::shared_ptr<MockAsyncFileAction>(new TypedMockAsyncFileAction(on_complete)));
      });
  ON_CALL(*this, openExistingFile(_, _, _))
      .WillByDefault([this](absl::string_view, Mode,
                            std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete) {
        return enqueue(
            std::shared_ptr<MockAsyncFileAction>(new TypedMockAsyncFileAction(on_complete)));
      });
  ON_CALL(*this, unlink(_, _))
      .WillByDefault([this](absl::string_view, std::function<void(absl::Status)> on_complete) {
        return enqueue(
            std::shared_ptr<MockAsyncFileAction>(new TypedMockAsyncFileAction(on_complete)));
      });
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
