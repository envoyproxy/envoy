#pragma once

#include <memory>
#include <string>

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/common/async_files/async_file_context_base.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

class AsyncFileManager;

// The thread pool implementation of an AsyncFileContext - uses the manager thread pool and
// old-school synchronous posix file operations.
class AsyncFileContextThreadPool final : public AsyncFileContextBase {
public:
  explicit AsyncFileContextThreadPool(AsyncFileManager& manager, int fd);

  // CancelFunction should not be called during or after the callback.
  // CancelFunction should only be called from the same thread that created
  // the context.
  // The callback will be dispatched to the same thread that created the context.
  absl::StatusOr<CancelFunction>
  stat(Event::Dispatcher* dispatcher,
       absl::AnyInvocable<void(absl::StatusOr<struct stat>)> on_complete) override;
  absl::StatusOr<CancelFunction>
  createHardLink(Event::Dispatcher* dispatcher, absl::string_view filename,
                 absl::AnyInvocable<void(absl::Status)> on_complete) override;
  absl::StatusOr<CancelFunction> close(Event::Dispatcher* dispatcher,
                                       absl::AnyInvocable<void(absl::Status)> on_complete) override;
  absl::StatusOr<CancelFunction>
  read(Event::Dispatcher* dispatcher, off_t offset, size_t length,
       absl::AnyInvocable<void(absl::StatusOr<Buffer::InstancePtr>)> on_complete) override;
  absl::StatusOr<CancelFunction>
  write(Event::Dispatcher* dispatcher, Buffer::Instance& contents, off_t offset,
        absl::AnyInvocable<void(absl::StatusOr<size_t>)> on_complete) override;
  absl::StatusOr<CancelFunction>
  duplicate(Event::Dispatcher* dispatcher,
            absl::AnyInvocable<void(absl::StatusOr<AsyncFileHandle>)> on_complete) override;

  int& fileDescriptor() { return file_descriptor_; }

  ~AsyncFileContextThreadPool() override;

protected:
  absl::StatusOr<CancelFunction> checkFileAndEnqueue(Event::Dispatcher* dispatcher,
                                                     std::unique_ptr<AsyncFileAction> action);

  int file_descriptor_;
};

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
