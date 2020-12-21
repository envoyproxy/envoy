#pragma once

#include <cstdint>

#include "envoy/event/file_event.h"

#include "common/event/dispatcher_impl.h"
#include "common/event/event_impl_base.h"

#include "extensions/io_socket/buffered_io_socket/peer_buffer.h"

namespace Envoy {

namespace Extensions {
namespace IoSocket {
namespace BufferedIoSocket {

// Return the enabled events except EV_CLOSED. This implementation is generally good since only
// epoll supports EV_CLOSED but the entire envoy code base supports another poller. The event owner
// must assume EV_CLOSED is never activated. Also event owner must tolerate that OS could notify
// events which are not actually triggered.
// TODO(lambdai): Add support of delivering EV_CLOSED.
class EventListenerImpl {
public:
  ~EventListenerImpl() = default;

  void clearEphemeralEvents();
  void onEventActivated(uint32_t activated_events);

  uint32_t getAndClearEphemeralEvents() { return std::exchange(ephemeral_events_, 0); }

private:
  // The events set by activate() and will be cleared after the io callback.
  uint32_t ephemeral_events_{};
};

// A FileEvent implementation which is used to drive BufferedIoSocketHandle.
// Declare the class final to safely call virtual function setEnabled in constructor.
class UserSpaceFileEventImpl final : public Event::FileEvent, Logger::Loggable<Logger::Id::io> {
public:
  UserSpaceFileEventImpl(Event::Dispatcher& dispatcher, Event::FileReadyCb cb, uint32_t events,
                         UserspaceIoHandle& io_source);

  // Event::FileEvent
  void activate(uint32_t events) override;
  void setEnabled(uint32_t events) override;

  // `UserspaceFileEvent` acts always as edge triggered regardless the underlying OS is level or
  // edge triggered. The event owner on windows platform should not emulate edge events.
  void unregisterEventIfEmulatedEdge(uint32_t) override {}
  void registerEventIfEmulatedEdge(uint32_t) override {}

private:
  // Used to populate the event operations of enable and activate.
  EventListenerImpl event_listener_;

  // The handle to registered async callback from dispatcher.
  Event::SchedulableCallbackPtr schedulable_;

  // Supplies readable and writable status.
  UserspaceIoHandle& io_source_;
};
} // namespace BufferedIoSocket
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
