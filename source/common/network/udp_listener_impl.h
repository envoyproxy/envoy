#pragma once

#include <atomic>

#include "common/buffer/buffer_impl.h"
#include "common/event/event_impl_base.h"
#include "common/event/file_event_impl.h"

#include "base_listener_impl.h"

namespace Envoy {
namespace Network {

/**
 * libevent implementation of Network::Listener for UDP.
 */
class UdpListenerImpl : public BaseListenerImpl,
                        public virtual UdpListener,
                        protected Logger::Loggable<Logger::Id::udp> {
public:
  UdpListenerImpl(Event::DispatcherImpl& dispatcher, Socket& socket, UdpListenerCallbacks& cb);

  ~UdpListenerImpl();

  // Network::Listener Interface
  void disable() override;
  void enable() override;

  // Network::UdpListener Interface
  Event::Dispatcher& dispatcher() override;
  const Address::InstanceConstSharedPtr& localAddress() const override;
  void send(const UdpSendData& data) override;

  struct ReceiveResult {
    Api::SysCallIntResult result_;
    Buffer::InstancePtr buffer_;
  };

  // Test overrides for mocking
  virtual ReceiveResult doRecvFrom(sockaddr_storage& peer_addr, socklen_t& addr_len);

protected:
  void handleWriteCallback();
  void handleReadCallback();

  UdpListenerCallbacks& cb_;

private:
  void onSocketEvent(short flags);
  Api::SysCallSizeResult sendmessage(const Buffer::RawSlice* slices, uint64_t num_slice,
                                     const Address::InstanceConstSharedPtr& to_address);
  Event::FileEventPtr file_event_;
};

} // namespace Network
} // namespace Envoy
