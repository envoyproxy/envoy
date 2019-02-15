#include "common/memory/heap_shrinker.h"

#include "common/memory/utils.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Memory {

// TODO(eziskind): make this configurable
constexpr std::chrono::milliseconds kTimerInterval = std::chrono::milliseconds(10000);

HeapShrinker::HeapShrinker(Event::Dispatcher& dispatcher, Server::OverloadManager& overload_manager,
                           Stats::Scope& stats)
    : active_(false) {
  const auto action_name = Server::OverloadActionNames::get().ShrinkHeap;
  if (overload_manager.registerForAction(action_name, dispatcher,
                                         [this](Server::OverloadActionState state) {
                                           active_ = (state == Server::OverloadActionState::Active);
                                         })) {
    shrink_counter_ = &stats.counter(absl::StrCat("overload.", action_name, ".shrink_count"));
    timer_ = dispatcher.createTimer([this] {
      shrinkHeap();
      timer_->enableTimer(kTimerInterval);
    });
    timer_->enableTimer(kTimerInterval);
  }
}

void HeapShrinker::shrinkHeap() {
  if (active_) {
    Utils::releaseFreeMemory();
    shrink_counter_->inc();
  }
}

} // namespace Memory
} // namespace Envoy
