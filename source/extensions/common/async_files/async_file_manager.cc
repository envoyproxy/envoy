#include "source/extensions/common/async_files/async_file_manager.h"

#include <memory>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#include "source/extensions/common/async_files/async_file_context_basic.h"
#include "source/extensions/common/async_files/async_file_context_impl.h"
#include "source/extensions/common/async_files/async_file_handle.h"

#include "absl/base/thread_annotations.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

class AsyncFileManagerImpl : public AsyncFileManager {
public:
  explicit AsyncFileManagerImpl(const AsyncFileManagerConfig& config) {
    unsigned int thread_pool_size = config.thread_pool_size;
    if (thread_pool_size == 0) {
      thread_pool_size = std::thread::hardware_concurrency();
    }
    thread_pool_.reserve(thread_pool_size);
    while (thread_pool_.size() < thread_pool_size) {
      thread_pool_.emplace_back([this]() { worker(); });
    }
  }

  ~AsyncFileManagerImpl() ABSL_LOCKS_EXCLUDED(queue_mutex_) override {
    {
      absl::MutexLock lock(&queue_mutex_);
      terminate_ = true;
    }
    while (!thread_pool_.empty()) {
      thread_pool_.back().join();
      thread_pool_.pop_back();
    }
  }

  AsyncFileHandle newFileHandle() override { return AsyncFileContextBasic::create(this); }

  unsigned int threadPoolSize() const override { return thread_pool_.size(); }

private:
  void enqueue(const std::shared_ptr<AsyncFileContextImpl> context)
      ABSL_LOCKS_EXCLUDED(queue_mutex_) override {
    absl::MutexLock lock(&queue_mutex_);
    queue_.push(context);
  }

  void worker() ABSL_LOCKS_EXCLUDED(queue_mutex_) {
    while (true) {
      std::shared_ptr<AsyncFileContextImpl> context;
      const auto condition = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(queue_mutex_) {
        return !queue_.empty() || terminate_;
      };
      {
        absl::MutexLock lock(&queue_mutex_);
        queue_mutex_.Await(absl::Condition(&condition));
        if (queue_.empty()) {
          return;
        }
        context = std::move(queue_.front());
        queue_.pop();
      }
      context->resolve();
    }
  }
  absl::Mutex queue_mutex_;
  std::queue<std::shared_ptr<AsyncFileContextImpl>> queue_ ABSL_GUARDED_BY(queue_mutex_);
  bool terminate_ ABSL_GUARDED_BY(queue_mutex_) = false;
  std::vector<std::thread> thread_pool_;
};

std::unique_ptr<AsyncFileManager> AsyncFileManagerConfig::createManager() const {
  return std::make_unique<AsyncFileManagerImpl>(*this);
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
