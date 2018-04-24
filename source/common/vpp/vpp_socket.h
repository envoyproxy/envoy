#pragma once

#include <cstdint>
#include <string>

#include "envoy/network/transport_socket.h"
#include "envoy/buffer/buffer.h"

#include "common/common/non_copyable.h"
#include "common/common/logger.h"
#include "common/event/dispatcher_impl.h"
#include <vcl/vppcom.h>
/*
 * alagalah - goal is to remove this include
 */
#include "event2/buffer.h"
#include "common/event/libevent.h"
#include "common/common/c_smart_ptr.h"
#include "event2/event.h"

namespace Envoy {
namespace Vpp {

class VppSocket : public Network::TransportSocket,
                  protected Logger::Loggable<Logger::Id::connection> {
public:
  // Network::TransportSocket
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override;
  bool canFlushClose() override { return true; }
  void closeSocket(Network::ConnectionEvent close_type) override;
  void onConnected() override;
  Network::IoResult doRead(Buffer::Instance& read_buffer) override;
  Network::IoResult doWrite(Buffer::Instance& write_buffer, bool end_stream) override;
  Ssl::Connection* ssl() override { return nullptr; }
  const Ssl::Connection* ssl() const override { return nullptr; }
private:
  Network::TransportSocketCallbacks* callbacks_{};
  // Maybe this is where I put reference to singleton VPPAPI connection?  bssl::UniquePtr<SSL> ssl_;
};

typedef std::unique_ptr<VppSocket> VppSocketPtr;

class VppSocketFactory : public Network::TransportSocketFactory {
public:
  // Network::TransportSocketFactory
  Network::TransportSocketPtr createTransportSocket() const override;
  bool implementsSecureTransport() const override;
};

} // namespace Vpp
} // namespace Envoy
