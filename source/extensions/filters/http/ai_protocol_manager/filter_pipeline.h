#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "source/common/coroutine/async_queue.h"
#include "source/common/coroutine/task.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Inter-stage queue pipeline used by RequestFilterManager::AsyncState and
// ResponseFilterManager::AsyncState.
//
// Manages N + 1 single-slot stage queues (0..N-1 for filters, N for the sink) and provides
// receive/propagate primitives and teardown queue draining.
template <typename Item> class FilterPipeline {
public:
  using StageQueue = Coroutine::AsyncQueue<Item>;
  using StageQueueSharedPtr = std::shared_ptr<StageQueue>;

  explicit FilterPipeline(size_t num_filters) {
    stages_.reserve(num_filters + 1);
    for (size_t i = 0; i <= num_filters; ++i) {
      stages_.push_back(std::make_shared<StageQueue>(/*max_size=*/1));
    }
  }

  bool closed() const { return closed_; }

  const StageQueueSharedPtr& stage(size_t index) const { return stages_[index]; }

  size_t numStages() const { return stages_.size(); }

  // Takes the next item for stage `index`; nullopt once the stage queue has been closed.
  Coroutine::Task<absl::StatusOr<std::optional<Item>>> receive(size_t index) {
    if (closed_) {
      co_return absl::CancelledError("filter pipeline closed");
    }
    co_return co_await stages_[index]->pop();
  }

  // Hands an item from stage `index` to the next stage (`index + 1`).
  Coroutine::Task<absl::Status> propagate(size_t index, Item item) {
    if (closed_) {
      co_return absl::CancelledError("filter pipeline closed");
    }
    co_return co_await stages_[index + 1]->push(std::move(item));
  }

  // Closes all stage queues and drains any buffered items so owned resources (such as per-frame
  // BufferManagers) are released immediately.
  void closeAndDrain() {
    closed_ = true;
    for (auto& stage : stages_) {
      stage->close();
      while (stage->tryPop().has_value()) {
      }
    }
  }

private:
  std::vector<StageQueueSharedPtr> stages_;
  bool closed_{false};
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
