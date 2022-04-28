#pragma once

#include <cstdint>

#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"

#include "source/common/common/assert.h"
#include "source/extensions/io_socket/user_space/io_handle.h"

namespace Envoy {

namespace Extensions {
namespace IoSocket {
namespace UserSpace {

// A FileEvent implementation which is used to drive UserSpaceHandle.
// Declare the class final to safely call virtual function setEnabled in constructor.
class FileEventImpl final : public Event::FileEvent, Logger::Loggable<Logger::Id::io> {
public:
  FileEventImpl(Event::Dispatcher& dispatcher, Event::FileReadyCb cb, uint32_t events,
                IoHandle& io_source);

  // Event::FileEvent
  void activate(uint32_t events) override;
  void setEnabled(uint32_t events) override;

  // This event always acts as edge triggered regardless the underlying OS is level or
  // edge triggered. The event owner on windows platform should not emulate edge events.
  void unregisterEventIfEmulatedEdge(uint32_t) override {}
  void registerEventIfEmulatedEdge(uint32_t) override {}

  // Notify events. Unlike activate() method, this method activates the given events only if the
  // events are enabled.
  void activateIfEnabled(uint32_t events);

private:
  // This class maintains the ephemeral events and enabled events.
  class EventListener {
  public:
    ~EventListener() = default;

    // Reset the enabled events. The caller must refresh the triggered events.
    void setEnabledEvents(uint32_t enabled_events) { enabled_events_ = enabled_events; }

    // Return the enabled events.
    uint32_t getEnabledEvents() { return enabled_events_; }

    void clearEphemeralEvents() {
      // Clear ephemeral events to align with FileEventImpl::setEnabled().
      ephemeral_events_ = 0;
    }

    void onEventActivated(uint32_t activated_events) { ephemeral_events_ |= activated_events; }

    uint32_t getAndClearEphemeralEvents() { return std::exchange(ephemeral_events_, 0); }

  private:
    // The events set by activate() and will be cleared after the io callback.
    uint32_t ephemeral_events_{};
    // The events set by setEnabled(). The new value replaces the old value.
    uint32_t enabled_events_{};
  };

  // Used to populate the event operations of enable and activate.
  EventListener event_listener_;

  // The handle to registered async callback from dispatcher.
  Event::SchedulableCallbackPtr schedulable_;

  // Supplies readable and writable status.
  IoHandle& io_source_;
};
} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
