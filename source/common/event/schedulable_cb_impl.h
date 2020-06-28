#pragma once

#include "envoy/event/schedulable_cb.h"

#include "common/event/event_impl_base.h"
#include "common/event/libevent.h"

namespace Envoy {
namespace Event {

class DispatcherImpl;

/**
 * libevent implementation of SchedulableCallback.
 */
class SchedulableCallbackImpl : public SchedulableCallback, ImplBase {
public:
  SchedulableCallbackImpl(Libevent::BasePtr& libevent, std::function<void()> cb);

  // SchedulableCallback implementation.
  void scheduleCallbackCurrentIteration() override;
  void scheduleCallbackNextIteration() override;
  void cancel() override;
  bool enabled() override;

private:
  std::function<void()> cb_;
};

} // namespace Event
} // namespace Envoy
