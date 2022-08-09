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

  absl::StatusOr<CancelFunction>
  createHardLink(absl::string_view filename,
                 std::function<void(absl::Status)> on_complete) override;
  absl::Status close(std::function<void(absl::Status)> on_complete) override;
  absl::StatusOr<CancelFunction>
  read(off_t offset, size_t length,
       std::function<void(absl::StatusOr<Buffer::InstancePtr>)> on_complete) override;
  absl::StatusOr<CancelFunction>
  write(Buffer::Instance& contents, off_t offset,
        std::function<void(absl::StatusOr<size_t>)> on_complete) override;
  absl::StatusOr<CancelFunction>
  duplicate(std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete) override;

  int& fileDescriptor() { return file_descriptor_; }

  ~AsyncFileContextThreadPool() override;

protected:
  absl::StatusOr<CancelFunction> checkFileAndEnqueue(std::shared_ptr<AsyncFileAction> action);

  int file_descriptor_;
};

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
