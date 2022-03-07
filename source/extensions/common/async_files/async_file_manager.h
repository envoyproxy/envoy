#pragma once

#include <memory>

#include "source/extensions/common/async_files/async_file_handle.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

class AsyncFileContextImpl;
class AsyncFileManager;

struct AsyncFileManagerConfig {
  // A default thread pool size of 0 will use std::thread::hardware_concurrency() for the number of
  // threads.
  unsigned int thread_pool_size = 0;

  // Create an AsyncFileManager. This must outlive all AsyncFileHandles it generates.
  std::unique_ptr<AsyncFileManager> createManager() const;
};

class AsyncFileManager {
public:
  virtual AsyncFileHandle newFileHandle() = 0;

  virtual ~AsyncFileManager() = default;

  virtual unsigned int threadPoolSize() const = 0;

private:
  virtual void enqueue(const std::shared_ptr<AsyncFileContextImpl> context) = 0;

  friend class AsyncFileContextImpl;
};

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
