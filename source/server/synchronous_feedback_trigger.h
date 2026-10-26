#pragma once

#include <utility>

#include "envoy/server/resource_monitor.h"

#include "source/server/trigger.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

/**
 * Combines a SynchronousFeedbackResourceMonitor and its associated Trigger into a single
 * interface for synchronous hot-path evaluation and load admission notification.
 */
class SynchronousFeedbackTrigger {
public:
  SynchronousFeedbackTrigger(TriggerPtr trigger,
                             SynchronousFeedbackResourceMonitorSharedPtr monitor)
      : trigger_(std::move(trigger)), monitor_(std::move(monitor)) {}

  // Queries the synchronous feedback monitor's current resource usage and evaluates the
  // resulting shed probability without mutating trigger state.
  float shedProbability() const {
    const double pressure = monitor_->getResourceUsage().resource_pressure_;
    return trigger_->evaluate(pressure).value().value();
  }

  // Notifies the underlying synchronous feedback monitor that load was accepted at the given point.
  void onLoadAccepted(absl::string_view load_shed_point_name) const {
    monitor_->onLoadAccepted(load_shed_point_name);
  }

private:
  TriggerPtr trigger_;
  SynchronousFeedbackResourceMonitorSharedPtr monitor_;
};

} // namespace Server
} // namespace Envoy
