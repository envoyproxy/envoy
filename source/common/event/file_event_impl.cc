#include "common/event/file_event_impl.h"

#include <cstdint>

#include "common/common/assert.h"
#include "common/event/dispatcher_impl.h"
#include "common/runtime/runtime_features.h"

#include "event2/event.h"

namespace Envoy {
namespace Event {

FileEventImpl::FileEventImpl(DispatcherImpl& dispatcher, os_fd_t fd, FileReadyCb cb,
                             FileTriggerType trigger, uint32_t events)
    : cb_(cb), fd_(fd), trigger_(trigger),
      activate_fd_events_next_event_loop_(Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.activate_fds_next_event_loop")) {
#ifdef WIN32
  RELEASE_ASSERT(trigger_ == FileTriggerType::Level,
                 "libevent does not support edge triggers on Windows");
#endif
  assignEvents(events, &dispatcher.base());
  event_add(&raw_event_, nullptr);
  if (activate_fd_events_next_event_loop_) {
    activation_cb_ = dispatcher.createSchedulableCallback([this]() {
      ASSERT(injected_activation_events_ != 0);
      mergeInjectedEventsAndRunCb(0);
    });
  }
}

void FileEventImpl::activate(uint32_t events) {
  // events is not empty.
  ASSERT(events != 0);
  // Only supported event types are set.
  ASSERT((events & (FileReadyType::Read | FileReadyType::Write | FileReadyType::Closed)) == events);

  if (!activate_fd_events_next_event_loop_) {
    // Legacy implementation
    int libevent_events = 0;
    if (events & FileReadyType::Read) {
      libevent_events |= EV_READ;
    }

    if (events & FileReadyType::Write) {
      libevent_events |= EV_WRITE;
    }

    if (events & FileReadyType::Closed) {
      libevent_events |= EV_CLOSED;
    }

    ASSERT(libevent_events);
    event_active(&raw_event_, libevent_events, 0);
    return;
  }

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

void FileEventImpl::assignEvents(uint32_t events, event_base* base) {
  ASSERT(base != nullptr);
  event_assign(
      &raw_event_, base, fd_,
      EV_PERSIST | (trigger_ == FileTriggerType::Level ? 0 : EV_ET) |
          (events & FileReadyType::Read ? EV_READ : 0) |
          (events & FileReadyType::Write ? EV_WRITE : 0) |
          (events & FileReadyType::Closed ? EV_CLOSED : 0),
      [](evutil_socket_t, short what, void* arg) -> void {
        auto* event = static_cast<FileEventImpl*>(arg);
        uint32_t events = 0;
        if (what & EV_READ) {
          events |= FileReadyType::Read;
        }

        if (what & EV_WRITE) {
          events |= FileReadyType::Write;
        }

        if (what & EV_CLOSED) {
          events |= FileReadyType::Closed;
        }

        ASSERT(events != 0);
        event->mergeInjectedEventsAndRunCb(events);
      },
      this);
}

void FileEventImpl::setEnabled(uint32_t events) {
  if (activate_fd_events_next_event_loop_ && injected_activation_events_ != 0) {
    // Clear pending events on updates to the fd event mask to avoid delivering events that are no
    // longer relevant. Updating the event mask will reset the fd edge trigger state so the proxy
    // will be able to determine the fd read/write state without need for the injected activation
    // events.
    injected_activation_events_ = 0;
    activation_cb_->cancel();
  }

  auto* base = event_get_base(&raw_event_);
  event_del(&raw_event_);
  assignEvents(events, base);
  event_add(&raw_event_, nullptr);
}

void FileEventImpl::mergeInjectedEventsAndRunCb(uint32_t events) {
  if (activate_fd_events_next_event_loop_ && injected_activation_events_ != 0) {
    events |= injected_activation_events_;
    injected_activation_events_ = 0;
    activation_cb_->cancel();
  }
  cb_(events);
}

} // namespace Event
} // namespace Envoy
