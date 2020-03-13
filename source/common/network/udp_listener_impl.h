#pragma once

#include <atomic>

#include "envoy/common/time.h"

#include "common/buffer/buffer_impl.h"
#include "common/event/event_impl_base.h"
#include "common/event/file_event_impl.h"
#include "common/network/utility.h"

#include "base_listener_impl.h"

namespace Envoy {
namespace Network {

/**
 * libevent implementation of Network::Listener for UDP.
 */
class UdpListenerImpl : public BaseListenerImpl,
                        public virtual UdpListener,
                        public UdpPacketProcessor,
                        protected Logger::Loggable<Logger::Id::udp> {
public:
  UdpListenerImpl(Event::DispatcherImpl& dispatcher, SocketSharedPtr socket,
                  UdpListenerCallbacks& cb, TimeSource& time_source);

  ~UdpListenerImpl() override;

  // Network::Listener Interface
  void disable() override;
  void enable() override;

  // Network::UdpListener Interface
  Event::Dispatcher& dispatcher() override;
  const Address::InstanceConstSharedPtr& localAddress() const override;
  Api::IoCallUint64Result send(const UdpSendData& data) override;

  void processPacket(Address::InstanceConstSharedPtr local_address,
                     Address::InstanceConstSharedPtr peer_address, Buffer::InstancePtr buffer,
                     MonotonicTime receive_time) override;

  uint64_t maxPacketSize() const override {
    // TODO(danzh) make this variable configurable to support jumbo frames.
    return MAX_UDP_PACKET_SIZE;
  }

protected:
  void handleWriteCallback();
  void handleReadCallback();

  UdpListenerCallbacks& cb_;
  uint32_t packets_dropped_{0};

private:
  void onSocketEvent(short flags);

  TimeSource& time_source_;
  Event::FileEventPtr file_event_;
};

} // namespace Network
} // namespace Envoy
