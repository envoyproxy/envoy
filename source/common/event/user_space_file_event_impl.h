#pragma once

#include <cstdint>

#include "envoy/event/file_event.h"

#include "common/event/dispatcher_impl.h"
#include "common/event/event_impl_base.h"
#include "common/network/peer_buffer.h"

namespace Envoy {

namespace Network {
class BufferedIoSocketHandleImpl;
}
namespace Event {

// The interface of populating event watcher and obtaining the active events. The events are the
// combination of FileReadyType values. The event listener is populated by user event registration
// and io events passively. Also the owner of this listener query the activated events by calling
// triggeredEvents and getAndClearEphemeralEvents.
class EventListener {
public:
  virtual ~EventListener() = default;

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

  void onEventEnabled(uint32_t enabled_events) override;
  void onEventActivated(uint32_t activated_events) override;

  uint32_t getAndClearEphemeralEvents() override { return std::exchange(ephemeral_events_, 0); }

private:
  // The events set by activate() and will be cleared after the io callback.
  uint32_t ephemeral_events_{};
};

// A FileEvent implementation which is used to drive BufferedIoSocketHandle.
// Declare the class final to safely call virtual function setEnabled in constructor.
class UserSpaceFileEventImpl final : public FileEvent, Logger::Loggable<Logger::Id::io> {
public:
  UserSpaceFileEventImpl(Event::Dispatcher& dispatcher, Event::FileReadyCb cb, uint32_t events,
                         Network::ReadWritable& io_source);

  ~UserSpaceFileEventImpl() override = default;

  // Event::FileEvent
  void activate(uint32_t events) override;
  void setEnabled(uint32_t events) override;

  friend class Network::BufferedIoSocketHandleImpl;

private:
  // Used to populate the event operations of enable and activate.
  EventListenerImpl event_listener_;

  // The handle to registered async callback from dispatcher.
  Event::SchedulableCallbackPtr schedulable_;

  // The registered callback of this event. This callback is usually on top of the frame of
  // Dispatcher::run().
  std::function<void()> cb_;

  Network::ReadWritable& io_source_;
};

} // namespace Event
} // namespace Envoy
