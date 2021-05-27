#pragma once

#include "include/envoy/event/dispatcher.h"
#include "include/envoy/server/overload/overload_manager.h"
#include "include/envoy/stats/scope.h"
#include "include/envoy/stats/stats.h"

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

  bool active_;
  Envoy::Stats::Counter* shrink_counter_;
  Envoy::Event::TimerPtr timer_;
};

} // namespace Memory
} // namespace Envoy
