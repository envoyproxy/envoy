#include "extensions/filters/udp/udp_proxy/udp_proxy_filter.h"

#include "envoy/network/listener.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {

void UdpProxyFilter::onData(Network::UdpRecvData& data) {
  const auto active_session_it = sessions_.find(data.addresses_);
  ActiveSession* active_session;
  if (active_session_it == sessions_.end()) {
    // TODO(mattklein123): Session circuit breaker.
    // TODO(mattklein123): Instead of looking up the cluster each time, keep track of it via
    // cluster manager callbacks.
    Upstream::ThreadLocalCluster* cluster = config_->getCluster();
    // TODO(mattklein123): Handle the case where the cluster does not exist and add stat.
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

void UdpProxyFilter::onReceiveError(Api::IoError::IoErrorCode) {
  config_->stats().downstream_sess_rx_errors_.inc();
}

UdpProxyFilter::ActiveSession::ActiveSession(UdpProxyFilter& parent,
                                             Network::UdpRecvData::LocalPeerAddresses&& addresses,
                                             const Upstream::HostConstSharedPtr& host)
    : parent_(parent), addresses_(std::move(addresses)), host_(host),
      idle_timer_(parent.read_callbacks_->udpListener().dispatcher().createTimer(
          [this] { onIdleTimer(); })),
      // NOTE: The socket call can only fail due to memory/fd exhaustion. No local ephemeral port
      //       is bound until the first packet is sent to the upstream host.
      io_handle_(parent.createIoHandle(host)),
      socket_event_(parent.read_callbacks_->udpListener().dispatcher().createFileEvent(
          io_handle_->fd(), [this](uint32_t) { onReadReady(); }, Event::FileTriggerType::Edge,
          Event::FileReadyType::Read)) {
  ENVOY_LOG(debug, "creating new session: downstream={} local={}", addresses_.peer_->asStringView(),
            addresses_.local_->asStringView());
  parent_.config_->stats().downstream_sess_total_.inc();
  parent_.config_->stats().downstream_sess_active_.inc();

  // TODO(mattklein123): Enable dropped packets socket option. In general the Socket abstraction
  // does not work well right now for client sockets. It's too heavy weight and is aimed at listener
  // sockets. We need to figure out how to either refactor Socket into something that works better
  // for this use case or allow the socket option abstractions to work directly against an IO
  // handle.
}

UdpProxyFilter::ActiveSession::~ActiveSession() {
  parent_.config_->stats().downstream_sess_active_.dec();
}

void UdpProxyFilter::ActiveSession::onIdleTimer() {
  ENVOY_LOG(debug, "session idle timeout: downstream={} local={}", addresses_.peer_->asStringView(),
            addresses_.local_->asStringView());
  parent_.config_->stats().idle_timeout_.inc();
  parent_.sessions_.erase(addresses_);
}

void UdpProxyFilter::ActiveSession::onReadReady() {
  idle_timer_->enableTimer(parent_.config_->sessionTimeout());

  // TODO(mattklein123): We should not be passing *addresses_.local_ to this function as we are
  //                     not trying to populate the local address for received packets.
  uint32_t packets_dropped = 0;
  const Api::IoErrorPtr result = Network::Utility::readPacketsFromSocket(
      *io_handle_, *addresses_.local_, *this, parent_.config_->timeSource(), packets_dropped);
  // TODO(mattklein123): Handle no error when we limit the number of packets read.
  if (result->getErrorCode() != Api::IoError::IoErrorCode::Again) {
    // TODO(mattklein123): Upstream cluster RX error stat.
  }
}

void UdpProxyFilter::ActiveSession::write(const Buffer::Instance& buffer) {
  ENVOY_LOG(trace, "writing {} byte datagram upstream: downstream={} local={} upstream={}",
            buffer.length(), addresses_.peer_->asStringView(), addresses_.local_->asStringView(),
            host_->address()->asStringView());
  parent_.config_->stats().downstream_sess_rx_bytes_.add(buffer.length());
  parent_.config_->stats().downstream_sess_rx_datagrams_.inc();

  idle_timer_->enableTimer(parent_.config_->sessionTimeout());

  // NOTE: On the first write, a local ephemeral port is bound, and thus this write can fail due to
  //       port exhaustion.
  // NOTE: We do not specify the local IP to use for the sendmsg call. We allow the OS to select
  //       the right IP based on outbound routing rules.
  Api::IoCallUint64Result rc =
      Network::Utility::writeToSocket(*io_handle_, buffer, nullptr, *host_->address());
  if (!rc.ok()) {
    // TODO(mattklein123): Upstream cluster TX error stat.
  } else {
    // TODO(mattklein123): Upstream cluster TX byte/datagram stats.
  }
}

void UdpProxyFilter::ActiveSession::processPacket(Network::Address::InstanceConstSharedPtr,
                                                  Network::Address::InstanceConstSharedPtr,
                                                  Buffer::InstancePtr buffer, MonotonicTime) {
  ENVOY_LOG(trace, "writing {} byte datagram downstream: downstream={} local={} upstream={}",
            buffer->length(), addresses_.peer_->asStringView(), addresses_.local_->asStringView(),
            host_->address()->asStringView());
  const uint64_t buffer_length = buffer->length();

  // TODO(mattklein123): Upstream cluster RX byte/datagram stats.

  Network::UdpSendData data{addresses_.local_->ip(), *addresses_.peer_, *buffer};
  const Api::IoCallUint64Result rc = parent_.read_callbacks_->udpListener().send(data);
  if (!rc.ok()) {
    parent_.config_->stats().downstream_sess_tx_errors_.inc();
  } else {
    parent_.config_->stats().downstream_sess_tx_bytes_.add(buffer_length);
    parent_.config_->stats().downstream_sess_tx_datagrams_.inc();
  }
}

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
