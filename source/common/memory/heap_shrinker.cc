#include "source/common/memory/heap_shrinker.h"

#include "source/common/memory/utils.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/symbol_table.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Memory {

namespace {
constexpr std::chrono::milliseconds kDefaultTimerInterval = std::chrono::milliseconds(10000);
constexpr uint64_t kDefaultMaxUnfreedMemoryBytes = 100 * 1024 * 1024;
} // namespace

HeapShrinker::HeapShrinker(Event::Dispatcher& dispatcher, Server::OverloadManager& overload_manager,
                           Stats::Scope& stats)
    : timer_interval_(kDefaultTimerInterval),
      max_unfreed_memory_bytes_(kDefaultMaxUnfreedMemoryBytes) {
  const auto shrink_heap_config = overload_manager.getShrinkHeapConfig();
  if (shrink_heap_config.has_value()) {
    if (shrink_heap_config->has_timer_interval()) {
      timer_interval_ = std::chrono::milliseconds(
          DurationUtil::durationToMilliseconds(shrink_heap_config->timer_interval()));
    }
    if (shrink_heap_config->max_unfreed_memory_bytes() > 0) {
      max_unfreed_memory_bytes_ = shrink_heap_config->max_unfreed_memory_bytes();
    }
  }

  const auto action_name = Server::OverloadActionNames::get().ShrinkHeap;
  if (overload_manager.registerForAction(
          action_name, dispatcher,
          [this](Server::OverloadActionState state) { active_ = state.isSaturated(); })) {
    Envoy::Stats::StatNameManagedStorage stat_name(
        absl::StrCat("overload.", action_name, ".shrink_count"), stats.symbolTable());
    shrink_counter_ = &stats.counterFromStatName(stat_name.statName());
    timer_ = dispatcher.createTimer([this] {
      shrinkHeap();
      timer_->enableTimer(timer_interval_);
    });
    timer_->enableTimer(timer_interval_);
  }
}

void HeapShrinker::shrinkHeap() {
  if (active_) {
    Utils::releaseFreeMemory(max_unfreed_memory_bytes_);
    shrink_counter_->inc();
  }
}

} // namespace Memory
} // namespace Envoy
