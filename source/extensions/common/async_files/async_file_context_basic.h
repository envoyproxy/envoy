#pragma once

#include <memory>
#include <string>

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/common/async_files/async_file_context_impl.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

// The basic implementation of an AsyncFileContext - uses the manager thread pool and old-school
// synchronous posix file operations.
class AsyncFileContextBasic : public AsyncFileContextImpl {
public:
  void createAnonymousFile(std::string path,
                           std::function<void(absl::Status)> on_complete) override;
  void createHardLink(std::string filename, std::function<void(absl::Status)> on_complete) override;
  void unlink(std::string filename, std::function<void(absl::Status)> on_complete) override;
  void openExistingFile(std::string filename, Mode mode,
                        std::function<void(absl::Status)> on_complete) override;
  void close(std::function<void(absl::Status)> on_complete) override;
  void read(off_t offset, size_t length,
            std::function<void(absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>>)>
                on_complete) override;
  void write(Envoy::Buffer::Instance& contents, off_t offset, // NOLINT(runtime/references)
             std::function<void(absl::StatusOr<size_t>)> on_complete) override;
  absl::StatusOr<std::shared_ptr<AsyncFileContext>> duplicate() override;

  int file_descriptor_; // Unguarded, only touched by worker thread.

  ~AsyncFileContextBasic() override;

protected:
  void abortClose() override;

private:
  // Private constructor so only accessible to manager and tests.
  explicit AsyncFileContextBasic(AsyncFileManager* manager, int fd = -1);
  static std::shared_ptr<AsyncFileContextImpl> create(AsyncFileManager* manager);
  friend class AsyncFileHandleTest;
  friend class AsyncFileManagerImpl;
};

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
