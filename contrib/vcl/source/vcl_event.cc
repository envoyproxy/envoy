#include "contrib/vcl/source/vcl_event.h"

#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace Vcl {

VclEvent::VclEvent(Event::Dispatcher& dispatcher, VclIoHandle& io_handle, Event::FileReadyCb cb)
    : cb_(cb), io_handle_(io_handle), activation_cb_(dispatcher.createSchedulableCallback([this]() {
        ASSERT(injected_activation_events_ != 0);
        mergeInjectedEventsAndRunCb();
      })) {}

VclEvent::~VclEvent() {
  // Worker listeners are valid only as long as the event is valid
  if (io_handle_.isWrkListener()) {
    VclIoHandle* parentListener = io_handle_.getParentListener();
    if (parentListener) {
      parentListener->clearChildWrkListener();
    }
    if (VCL_SH_VALID(io_handle_.sh())) {
      io_handle_.close();
    }
  }
}

void VclEvent::activate(uint32_t events) {
  // events is not empty.
  ASSERT(events != 0);
  // Only supported event types are set.
  ASSERT((events & (Event::FileReadyType::Read | Event::FileReadyType::Write |
                    Event::FileReadyType::Closed)) == events);

  // Schedule the activation callback so it runs as part of the next loop iteration if it is not
  // already scheduled.
  if (injected_activation_events_ == 0) {
    ASSERT(!activation_cb_->enabled());
    activation_cb_->scheduleCallbackNextIteration();
  }
  ASSERT(activation_cb_->enabled());

  // Merge new events with pending injected events.
  injected_activation_events_ |= events;
}

void VclEvent::setEnabled(uint32_t events) { io_handle_.updateEvents(events); }

void VclEvent::mergeInjectedEventsAndRunCb() {
  uint32_t events = 0;
  if (injected_activation_events_ != 0) {
    events |= injected_activation_events_;
    injected_activation_events_ = 0;
    activation_cb_->cancel();
  }
  THROW_IF_NOT_OK(cb_(events));
}

void VclEvent::unregisterEventIfEmulatedEdge(uint32_t) {}

void VclEvent::registerEventIfEmulatedEdge(uint32_t) {}

} // namespace Vcl
} // namespace Network
} // namespace Extensions
} // namespace Envoy
