#pragma once

#include <functional>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/common/time.h"
#include "envoy/event/evwatch.h"
#include "envoy/event/evwatch_observer_manager.h"

#include "event2/event.h"
#include "event2/watch.h"

namespace Envoy {
namespace Event {

/**
 * Manages Evwatch prepare/check hooks and observer registration for a libevent event_base.
 */
class EvwatchObserverManagerImpl : public EvwatchObserverManager {
public:
  EvwatchObserverManagerImpl(event_base& libevent, TimeSource& time_source);
  ~EvwatchObserverManagerImpl() override;

  void registerObserver(Evwatch::Observer& observer) override;
  void unregisterObserver(Evwatch::Observer& observer) override;

private:
  static void onPrepare(evwatch* watch, const evwatch_prepare_cb_info* info, void* arg);
  static void onCheck(evwatch* watch, const evwatch_check_cb_info* info, void* arg);

  void cleanupNulledObservers();

  event_base& libevent_;
  TimeSource& time_source_;
  std::vector<OptRef<Evwatch::Observer>> observers_;
  bool hooks_registered_{false};
  uint32_t iteration_depth_{0};
  bool has_nulled_observers_{false};
  evwatch* prepare_evwatch_{nullptr};
  evwatch* check_evwatch_{nullptr};
};

} // namespace Event
} // namespace Envoy
