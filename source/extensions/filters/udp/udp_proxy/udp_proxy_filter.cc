#include "extensions/filters/udp/udp_proxy/udp_proxy_filter.h"

#include "envoy/network/listener.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {

// TODO(mattklein123): Logging
// TODO(mattklein123): Stats

void UdpProxyFilter::onData(Network::UdpRecvData& data) {
  const auto active_session_it = sessions_.find(data.addresses_);
  ActiveSession* active_session;
  if (active_session_it == sessions_.end()) {
    // TODO(mattklein123): Session circuit breaker.
    // TODO(mattklein123): Instead of looking up the cluster each time, keep track of it via
    // cluster manager callbacks.
    Upstream::ThreadLocalCluster* cluster = config_->getCluster();
    // TODO(mattklein123): Handle the case where the cluster does not exist.
    ASSERT(cluster != nullptr);

    // TODO(mattklein123): Pass a context and support hash based routing.
    Upstream::HostConstSharedPtr host = cluster->loadBalancer().chooseHost(nullptr);
    // TODO(mattklein123): Handle the case where the host does not exist.
    ASSERT(host != nullptr);

    auto new_session = std::make_unique<ActiveSession>(*this, std::move(data.addresses_), host);
    active_session = new_session.get();
    sessions_.emplace(std::move(new_session));
  } else {
    // TODO(mattklein123): Handle the host going away going away or failing health checks.
    active_session = active_session_it->get();
  }

  active_session->write(*data.buffer_);
}

UdpProxyFilter::ActiveSession::ActiveSession(UdpProxyFilter& parent,
                                             Network::UdpRecvData::LocalPeerAddresses&& addresses,
                                             const Upstream::HostConstSharedPtr& host)
    : parent_(parent), addresses_(std::move(addresses)), host_(host),
      // NOTE: The socket call can only fail due to memory/fd exhaustion. No local ephemeral port
      //       is bound until the first packet is sent to the upstream host.
      io_handle_(host->address()->socket(Network::Address::SocketType::Datagram)),
      socket_event_(parent.read_callbacks_->udpListener().dispatcher().createFileEvent(
          io_handle_->fd(), [this](uint32_t) { onReadReady(); }, Event::FileTriggerType::Edge,
          Event::FileReadyType::Read)) {
  ENVOY_LOG(debug, "creating new session: downstream={} local={}", addresses_.peer_->asStringView(),
            addresses_.local_->asStringView());

  // TODO(mattklein123): Enable dropped packets socket option. In general the Socket abstraction
  // does not work well right now for client sockets. It's too heavy weight and is aimed at listener
  // sockets. We need to figure out how to either refactor Socket into something that works better
  // for this use case or allow the socket option abstractions to work directly against an IO
  // handle.
}

void UdpProxyFilter::ActiveSession::onReadReady() {
  // TODO(mattklein123): Refresh idle timer.
  uint32_t packets_dropped = 0;
  const Api::IoErrorPtr result = Network::Utility::readPacketsFromSocket(
      *io_handle_, *addresses_.local_, *this, parent_.config_->timeSource(), packets_dropped);
  // TODO(mattklein123): Handle no error when we limit the number of packets read.
  // TODO(mattklein123): Increment stat on failure.
  ASSERT(result->getErrorCode() == Api::IoError::IoErrorCode::Again);
}

void UdpProxyFilter::ActiveSession::write(const Buffer::Instance& buffer) {
  ENVOY_LOG(trace, "writing {} byte datagram: downstream={} local={} upstream={}", buffer.length(),
            addresses_.peer_->asStringView(), addresses_.local_->asStringView(),
            host_->address()->asStringView());

  // TODO(mattklein123): Refresh idle timer.
  // NOTE: On the first write, a local ephemeral port is bound, and thus this write can fail due to
  //       port exhaustion.
  // NOTE: We do not specify the local IP to use for the sendmsg call. We allow the OS to select
  //       the right IP based on outbound routing rules.
  Api::IoCallUint64Result rc =
      Network::Utility::writeToSocket(*io_handle_, buffer, nullptr, *host_->address());
  // TODO(mattklein123): Increment stat on failure.
  ASSERT(rc.ok());
}

void UdpProxyFilter::ActiveSession::processPacket(Network::Address::InstanceConstSharedPtr,
                                                  Network::Address::InstanceConstSharedPtr,
                                                  Buffer::InstancePtr buffer, MonotonicTime) {
  Network::UdpSendData data{addresses_.local_->ip(), *addresses_.peer_, *buffer};
  const Api::IoCallUint64Result rc = parent_.read_callbacks_->udpListener().send(data);
  // TODO(mattklein123): Increment stat on failure.
  ASSERT(rc.ok());
}

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
