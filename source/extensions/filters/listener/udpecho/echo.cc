#include "extensions/filters/listener/udpecho/echo.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace UdpEcho {

void EchoFilter::onData(Network::UdpRecvData& data) {
  ENVOY_LOG(trace, "UdpEchoFilter: Got {} bytes from {}", data.buffer_->length(),
            data.peer_address_->asString());

  // send back the received data
  Network::UdpSendData send_data{data.peer_address_, *data.buffer_};
  read_callbacks_->udpListener().send(send_data);

  return;
}

} // namespace UdpEcho
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
