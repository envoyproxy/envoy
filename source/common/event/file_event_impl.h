#pragma once

#include <cstdint>

#include "envoy/event/file_event.h"

#include "common/event/dispatcher_impl.h"
#include "common/event/event_impl_base.h"

namespace Envoy {

namespace Network {
class BufferedIoSocketHandleImpl;
}
namespace Event {

/**
 * Implementation of FileEvent for libevent that uses persistent events and
 * assumes the user will read/write until EAGAIN is returned from the file.
 */
class FileEventImpl : public FileEvent, ImplBase {
public:
  FileEventImpl(DispatcherImpl& dispatcher, os_fd_t fd, FileReadyCb cb, FileTriggerType trigger,
                uint32_t events);

  // Event::FileEvent
  void activate(uint32_t events) override;
  void setEnabled(uint32_t events) override;

private:
  void assignEvents(uint32_t events, event_base* base);
  void mergeInjectedEventsAndRunCb(uint32_t events);

  FileReadyCb cb_;
  os_fd_t fd_;
  FileTriggerType trigger_;

  // Injected FileReadyType events that were scheduled by recent calls to activate() and are pending
  // delivery.
  uint32_t injected_activation_events_{};
  // Used to schedule delayed event activation. Armed iff pending_activation_events_ != 0.
  SchedulableCallbackPtr activation_cb_;
  // Latched "envoy.reloadable_features.activate_fds_next_event_loop" runtime feature. If true, fd
  // events scheduled via activate are evaluated in the next iteration of the event loop after
  // polling and activating new fd events.
  const bool activate_fd_events_next_event_loop_;
};

// Forward declare for friend class.
class UserSpaceFileEventFactory;

// The interface of populating event watcher and obtaining the active events. The events are the
// combination of FileReadyType values. The event listener is populated by user event registration
// and io events passively. Also the owner of this listener query the activated events by calling
// triggeredEvents and getAndClearEphemeralEvents.
class EventListener {
public:
  virtual ~EventListener() = default;

  // Get the events which are enabled and triggered.
  virtual uint32_t triggeredEvents() PURE;
  // Get the events which are ephemerally activated. Upon returning the ephemeral events are
  // cleared.
  virtual uint32_t getAndClearEphemeralEvents() PURE;

  /**
   * FileEvent::setEnabled is invoked.
   * @param enabled_events supplied the event of setEnabled.
   */
  virtual void onEventEnabled(uint32_t enabled_events) PURE;

  /**
   * FileEvent::activate is invoked.
   * @param enabled_events supplied the event of activate().
   */
  virtual void onEventActivated(uint32_t activated_events) PURE;
};

// Return the enabled events except EV_CLOSED. This implementation is generally good since only
// epoll supports EV_CLOSED but the entire envoy code base supports another poller. The event owner
// must assume EV_CLOSED is never activated. Also event owner must tolerate that OS could notify
// events which are not actually triggered.
// TODO(lambdai): Add support of delivering EV_CLOSED.
class DefaultEventListener : public EventListener {
public:
  ~DefaultEventListener() override = default;

  // Return both read and write.
  uint32_t triggeredEvents() override { return pending_events_ & (~Event::FileReadyType::Closed); }

  void onEventEnabled(uint32_t enabled_events) override { pending_events_ = enabled_events; }

  void onEventActivated(uint32_t activated_events) override {
    ephermal_events_ |= activated_events;
  }

  uint32_t getAndClearEphemeralEvents() override { return std::exchange(ephermal_events_, 0); }

private:
  // The persisted interested events and ready events.
  uint32_t pending_events_{};
  // The events set by activate() and will be cleared after the io callback.
  uint32_t ephermal_events_{};
};

// A FileEvent implementation which is used to drive BufferedIoSocketHandle.
class UserSpaceFileEventImpl : public FileEvent, Logger::Loggable<Logger::Id::io> {
public:
  ~UserSpaceFileEventImpl() override {
    if (schedulable_.enabled()) {
      schedulable_.cancel();
    }
  }

  // Event::FileEvent
  void activate(uint32_t events) override {
    // Only supported event types are set.
    ASSERT((events & (FileReadyType::Read | FileReadyType::Write | FileReadyType::Closed)) ==
           events);
    event_listener_.onEventActivated(events);
    if (!schedulable_.enabled()) {
      schedulable_.scheduleCallbackNextIteration();
    }
  }

  void setEnabled(uint32_t events) override {
    // Only supported event types are set.
    ASSERT((events & (FileReadyType::Read | FileReadyType::Write | FileReadyType::Closed)) ==
           events);
    event_listener_.onEventEnabled(events);
    bool was_enabled = schedulable_.enabled();
    if (!was_enabled) {
      schedulable_.scheduleCallbackNextIteration();
    }
    ENVOY_LOG(trace, "User space file event {} set events {}. Will {} reschedule.",
              static_cast<void*>(this), events, was_enabled ? "not " : "");
  }

  EventListener& getEventListener() { return event_listener_; }
  void onEvents() { cb_(); }
  friend class UserSpaceFileEventFactory;
  friend class Network::BufferedIoSocketHandleImpl;

private:
  UserSpaceFileEventImpl(Event::FileReadyCb cb, uint32_t events,
                         SchedulableCallback& schedulable_cb)
      : schedulable_(schedulable_cb), cb_([this, cb]() {
          auto all_events = getEventListener().triggeredEvents();
          auto ephemeral_events = getEventListener().getAndClearEphemeralEvents();
          ENVOY_LOG(trace,
                    "User space event {} invokes callbacks on allevents = {}, ephermal events = {}",
                    static_cast<void*>(this), all_events, ephemeral_events);
          cb(all_events | ephemeral_events);
        }) {
    event_listener_.onEventEnabled(events);
  }

  // Used to populate the event operations of enable and activate.
  DefaultEventListener event_listener_;

  // The handle to registered async callback from dispatcher.
  SchedulableCallback& schedulable_;

  // The registered callback of this event. This callback is usually on top of the frame of
  // Dispatcher::run().
  std::function<void()> cb_;
};

class UserSpaceFileEventFactory {
public:
  static std::unique_ptr<UserSpaceFileEventImpl>
  createUserSpaceFileEventImpl(Event::Dispatcher&, Event::FileReadyCb cb, Event::FileTriggerType,
                               uint32_t events, SchedulableCallback& scheduable_cb) {
    return std::unique_ptr<UserSpaceFileEventImpl>(
        new UserSpaceFileEventImpl(cb, events, scheduable_cb));
  }
};

} // namespace Event
} // namespace Envoy
