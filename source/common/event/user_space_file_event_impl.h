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
class EventListenerImpl : public EventListener {
public:
  ~EventListenerImpl() override = default;

  // Return both read and write if enabled. Note that this implementation is inefficient. Read and
  // write events are supposed to be independent.
  uint32_t triggeredEvents() override { return enabled_events_ & (~Event::FileReadyType::Closed); }

  void onEventEnabled(uint32_t enabled_events) override;
  void onEventActivated(uint32_t activated_events) override;

  uint32_t getAndClearEphemeralEvents() override { return std::exchange(ephemeral_events_, 0); }

private:
  // The persisted interested events. The name on libevent document is pending event.
  uint32_t enabled_events_{};
  // The events set by activate() and will be cleared after the io callback.
  uint32_t ephemeral_events_{};
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
  void activate(uint32_t events) override;
  void setEnabled(uint32_t events) override;

  EventListener& getEventListener() { return event_listener_; }
  void onEvents() { cb_(); }
  friend class UserSpaceFileEventFactory;
  friend class Network::BufferedIoSocketHandleImpl;

private:
  UserSpaceFileEventImpl(Event::FileReadyCb cb, uint32_t events,
                         SchedulableCallback& schedulable_cb);

  // Used to populate the event operations of enable and activate.
  EventListenerImpl event_listener_;

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
                               uint32_t events, SchedulableCallback& scheduable_cb);
};

} // namespace Event
} // namespace Envoy
