#pragma once

#include <utility>

#include "envoy/server/resource_monitor.h"

#include "source/server/trigger.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

/**
 * Combines a RealtimeResourceMonitor and its associated Trigger into a single
 * interface for synchronous hot-path evaluation and load admission notification.
 */
class RealtimeTrigger {
public:
  RealtimeTrigger(TriggerPtr trigger, RealtimeResourceMonitorSharedPtr monitor)
      : trigger_(std::move(trigger)), monitor_(std::move(monitor)) {}

  // Queries the real-time monitor's current resource usage and evaluates the
  // resulting shed probability without mutating trigger state.
  float shedProbability() const {
    const double pressure = monitor_->getResourceUsage().resource_pressure_;
    return trigger_->evaluate(pressure).value().value();
  }

  // Notifies the underlying real-time monitor that load was accepted at the given point.
  void onLoadAccepted(absl::string_view load_shed_point_name) const {
    monitor_->onLoadAccepted(load_shed_point_name);
  }

private:
  TriggerPtr trigger_;
  RealtimeResourceMonitorSharedPtr monitor_;
};

} // namespace Server
} // namespace Envoy
