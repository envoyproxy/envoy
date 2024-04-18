#pragma once

#include <cstdint>

#include "envoy/event/file_event.h"

#include "source/common/event/dispatcher_impl.h"
#include "source/common/event/event_impl_base.h"

#include "contrib/vcl/source/vcl_io_handle.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace Vcl {

class VclEvent : public Envoy::Event::FileEvent {
public:
  VclEvent(Event::Dispatcher& dispatcher, VclIoHandle& io_handle, Event::FileReadyCb cb);
  ~VclEvent() override;

  // Event::FileEvent
  void activate(uint32_t events) override;
  void setEnabled(uint32_t events) override;
  void unregisterEventIfEmulatedEdge(uint32_t event) override;
  void registerEventIfEmulatedEdge(uint32_t event) override;

private:
  void mergeInjectedEventsAndRunCb();

  Event::FileReadyCb cb_;
  VclIoHandle& io_handle_;

  // Injected FileReadyType events that were scheduled by recent calls to activate() and are pending
  // delivery.
  uint32_t injected_activation_events_{};
  // Used to schedule delayed event activation. Armed iff pending_activation_events_ != 0.
  Event::SchedulableCallbackPtr activation_cb_;
};

} // namespace Vcl
} // namespace Network
} // namespace Extensions
} // namespace Envoy
