#include "common/api/os_sys_calls_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/signal_impl.h"

#include "event2/event.h"

namespace Envoy {
namespace Event {

SignalEventImpl::SignalEventImpl(DispatcherImpl& dispatcher, signal_t signal_num, SignalCb cb)
    : cb_(cb) {

  if (signal_num > eventBridgeHandlersSingleton::get().size()) {
    ENVOY_BUG(false, "Attempting to create SignalEventImpl with a signal id that exceeds the "
                     "number of supported signals.");
    return;
  }

  if (eventBridgeHandlersSingleton::get()[signal_num]) {
    return;
  }
  os_fd_t socks[2];
  Api::SysCallIntResult result =
      Api::OsSysCallsSingleton::get().socketpair(AF_INET, SOCK_STREAM, IPPROTO_TCP, socks);
  ASSERT(result.rc_ == 0);

  read_handle_ = std::make_unique<Network::IoSocketHandleImpl>(socks[0], false, AF_INET);
  result = read_handle_->setBlocking(false);
  ASSERT(result.rc_ == 0);
  auto write_handle = std::make_shared<Network::IoSocketHandleImpl>(socks[1], false, AF_INET);
  result = write_handle->setBlocking(false);
  ASSERT(result.rc_ == 0);

  read_handle_->initializeFileEvent(
      dispatcher,
      [this](uint32_t events) -> void {
        ASSERT(events == Event::FileReadyType::Read);
        cb_();
      },
      Event::FileTriggerType::Level, Event::FileReadyType::Read);
  eventBridgeHandlersSingleton::get()[signal_num] = write_handle;
}

} // namespace Event
} // namespace Envoy
