#pragma once

#include <event2/event.h>

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

// The interface of populating event watcher and obtaining the active events. The events includes
// Read, Write and Closed.
class EventListener {
public:
  virtual ~EventListener() = default;

  // Provide the activated events.
  virtual uint32_t triggeredEvents() PURE;
  virtual uint32_t getAndClearEphemeralEvents() PURE;

  // Callbacks of the event operation.
  virtual void onEventEnabled(uint32_t enabled_events) PURE;
  virtual void onEventActivated(uint32_t activated_events) PURE;
};

// Return the enabled events except EV_CLOSED. This implementation is generally good since only
// epoll supports EV_CLOSED but the entire envoy code base supports other pollers. The event owner
// must assume EV_CLOSED is never activated. Also event owner must tolerat that OS could notify events
// which are not actually triggered.
class DefaultEventListener : public EventListener {
public:
  ~DefaultEventListener() override = default;
  uint32_t triggeredEvents() override {
    ENVOY_LOG_MISC(debug,
                   "lambdai: user file event listener triggered events {} on {} and schedule next",
                   pending_events_, static_cast<void*>(this));

    return pending_events_ & (~Event::FileReadyType::Closed);
  }
  void onEventEnabled(uint32_t enabled_events) override {
    ENVOY_LOG_MISC(
        debug, "lambdai: user file event listener set enabled events {} on {} and schedule next",
        pending_events_, static_cast<void*>(this));
    pending_events_ = enabled_events;
  }
  void onEventActivated(uint32_t activated_events) override {
    ephermal_events_ |= activated_events;
  }
  uint32_t getAndClearEphemeralEvents() override {
    auto res = ephermal_events_;
    ephermal_events_ = 0;
    return res;
  }

private:
  // The persisted interested events and ready events.
  uint32_t pending_events_{};
  // The events set by activate() and will be cleared after the io callback.
  uint32_t ephermal_events_{};
};

// A FileEvent implementation which is
class UserSpaceFileEventImpl : public FileEvent {
public:
  ~UserSpaceFileEventImpl() override {
    // if (schedulable_.enabled()) {
    schedulable_.cancel();
    //}
    ASSERT(event_counter_ == 1);
    --event_counter_;
  }

  // Event::FileEvent
  void activate(uint32_t events) override {
    event_listener_.onEventActivated(events);
    if (!schedulable_.enabled()) {
      schedulable_.scheduleCallbackNextIteration();
    }
  }

  void setEnabled(uint32_t events) override {
    event_listener_.onEventEnabled(events);
    if (!schedulable_.enabled()) {
      schedulable_.scheduleCallbackNextIteration();
      ENVOY_LOG_MISC(debug, "lambdai: user file event setEnabled {} on {} and schedule next",
                     events, static_cast<void*>(this));
      return;
    }
    ENVOY_LOG_MISC(debug, "lambdai: user file event setEnabled {} on {} and but not schedule next",
                   events, static_cast<void*>(this));
  }

  EventListener& getEventListener() { return event_listener_; }
  void onEvents() { cb_(); }
  friend class UserSpaceFileEventFactory;
  friend class Network::BufferedIoSocketHandleImpl;

private:
  UserSpaceFileEventImpl(Event::FileReadyCb cb, uint32_t events,
                         SchedulableCallback& schedulable_cb, int& event_counter)
      : schedulable_(schedulable_cb), cb_([this, cb]() {
          auto all_events = getEventListener().triggeredEvents();
          auto ephemeral_events = getEventListener().getAndClearEphemeralEvents();
          ENVOY_LOG_MISC(debug, "lambdai: us event {} cb allevents = {}, ephermal events = {}",
                         static_cast<void*>(this), all_events, ephemeral_events);
          cb(all_events | ephemeral_events);
        }),
        event_counter_(event_counter) {
    event_listener_.onEventEnabled(events);
  }
  DefaultEventListener event_listener_;
  SchedulableCallback& schedulable_;
  std::function<void()> cb_;
  int& event_counter_;
};

class UserSpaceFileEventFactory {
public:
  static std::unique_ptr<UserSpaceFileEventImpl>
  createUserSpaceFileEventImpl(Event::Dispatcher&, Event::FileReadyCb cb, Event::FileTriggerType,
                               uint32_t events, SchedulableCallback& scheduable_cb,
                               int& event_counter) {
    return std::unique_ptr<UserSpaceFileEventImpl>(
        new UserSpaceFileEventImpl(cb, events, scheduable_cb, event_counter));
  }
};

} // namespace Event
} // namespace Envoy
