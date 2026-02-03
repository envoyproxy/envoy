#pragma once

#include <chrono>
#include <cstdint>

#include "envoy/event/dispatcher.h"
#include "envoy/server/overload/overload_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

namespace Envoy {
namespace Memory {

/**
 * A utility class to periodically attempt to shrink the heap by releasing free memory
 * to the system if the "shrink heap" overload action has been configured and triggered.
 */
class HeapShrinker {
public:
  HeapShrinker(Event::Dispatcher& dispatcher, Server::OverloadManager& overload_manager,
               Envoy::Stats::Scope& stats);

private:
  void shrinkHeap();

  bool active_{false};
  Envoy::Stats::Counter* shrink_counter_;
  Envoy::Event::TimerPtr timer_;
  std::chrono::milliseconds timer_interval_;
  uint64_t max_unfreed_memory_bytes_;
};

} // namespace Memory
} // namespace Envoy
