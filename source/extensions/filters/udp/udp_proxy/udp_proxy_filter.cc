#include "extensions/filters/udp/udp_proxy/udp_proxy_filter.h"

#include "envoy/network/listener.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {

UdpProxyFilter::UdpProxyFilter(Network::UdpReadFilterCallbacks& callbacks,
                               const UdpProxyFilterConfigSharedPtr& config)
    : UdpListenerReadFilter(callbacks), config_(config),
      cluster_update_callbacks_(
          config->clusterManager().addThreadLocalClusterUpdateCallbacks(*this)) {
  Upstream::ThreadLocalCluster* cluster = config->clusterManager().get(config->cluster());
  if (cluster != nullptr) {
    onClusterAddOrUpdate(*cluster);
  }
}

void UdpProxyFilter::onClusterAddOrUpdate(Upstream::ThreadLocalCluster& cluster) {
  if (cluster.info()->name() != config_->cluster()) {
    return;
  }

  ENVOY_LOG(debug, "udp proxy: attaching to cluster {}", cluster.info()->name());
  ASSERT(cluster_info_ == absl::nullopt || &cluster_info_.value().cluster_ != &cluster);
  cluster_info_.emplace(*this, cluster);
}

void UdpProxyFilter::onClusterRemoval(const std::string& cluster) {
  if (cluster != config_->cluster()) {
    return;
  }

  ENVOY_LOG(debug, "udp proxy: detaching from cluster {}", cluster);
  cluster_info_.reset();
}

void UdpProxyFilter::onData(Network::UdpRecvData& data) {
  if (!cluster_info_.has_value()) {
    config_->stats().downstream_sess_no_route_.inc();
    return;
  }

  cluster_info_.value().onData(data);
}

void UdpProxyFilter::onReceiveError(Api::IoError::IoErrorCode) {
  config_->stats().downstream_sess_rx_errors_.inc();
}

UdpProxyFilter::ClusterInfo::ClusterInfo(UdpProxyFilter& filter,
                                         Upstream::ThreadLocalCluster& cluster)
    : filter_(filter), cluster_(cluster),
      cluster_stats_(generateStats(cluster.info()->statsScope())),
      member_update_cb_handle_(cluster.prioritySet().addMemberUpdateCb(
          [this](const Upstream::HostVector&, const Upstream::HostVector& hosts_removed) {
            for (const auto& host : hosts_removed) {
              // This is similar to removeSession() but slightly different due to removeSession()
              // also handling deletion of the host to session map entry if there are no sessions
              // left. It would be nice to unify the logic but that can be cleaned up later.
              auto host_sessions_it = host_to_sessions_.find(host.get());
              if (host_sessions_it != host_to_sessions_.end()) {
                for (const auto& session : host_sessions_it->second) {
                  ASSERT(sessions_.count(session) == 1);
                  sessions_.erase(session);
                }
                host_to_sessions_.erase(host_sessions_it);
              }
            }
          })) {}

UdpProxyFilter::ClusterInfo::~ClusterInfo() {
  member_update_cb_handle_->remove();
  // Sanity check the session accounting. This is not as fast as a straight teardown, but this is
  // not a performance critical path.
  while (!sessions_.empty()) {
    removeSession(sessions_.begin()->get());
  }
  ASSERT(host_to_sessions_.empty());
}

void UdpProxyFilter::ClusterInfo::onData(Network::UdpRecvData& data) {
  const auto active_session_it = sessions_.find(data.addresses_);
  ActiveSession* active_session;
  if (active_session_it == sessions_.end()) {
    if (!cluster_.info()
             ->resourceManager(Upstream::ResourcePriority::Default)
             .connections()
             .canCreate()) {
      cluster_.info()->stats().upstream_cx_overflow_.inc();
      return;
    }

    // TODO(mattklein123): Pass a context and support hash based routing.
    Upstream::HostConstSharedPtr host = cluster_.loadBalancer().chooseHost(nullptr);
    if (host == nullptr) {
      cluster_.info()->stats().upstream_cx_none_healthy_.inc();
      return;
    }

    active_session = createSession(std::move(data.addresses_), host);
  } else {
    active_session = active_session_it->get();
    if (active_session->host().health() == Upstream::Host::Health::Unhealthy) {
      // If a host becomes unhealthy, we optimally would like to replace it with a new session
      // to a healthy host. We may eventually want to make this behavior configurable, but for now
      // this will be the universal behavior.

      // TODO(mattklein123): Pass a context and support hash based routing.
      Upstream::HostConstSharedPtr host = cluster_.loadBalancer().chooseHost(nullptr);
      if (host != nullptr && host->health() != Upstream::Host::Health::Unhealthy &&
          host.get() != &active_session->host()) {
        ENVOY_LOG(debug, "upstream session unhealthy, recreating the session");
        removeSession(active_session);
        active_session = createSession(std::move(data.addresses_), host);
      } else {
        // In this case we could not get a better host, so just keep using the current session.
        ENVOY_LOG(trace, "upstream session unhealthy, but unable to get a better host");
      }
    }
  }

  active_session->write(*data.buffer_);
}

UdpProxyFilter::ActiveSession*
UdpProxyFilter::ClusterInfo::createSession(Network::UdpRecvData::LocalPeerAddresses&& addresses,
                                           const Upstream::HostConstSharedPtr& host) {
  auto new_session = std::make_unique<ActiveSession>(*this, std::move(addresses), host);
  auto new_session_ptr = new_session.get();
  sessions_.emplace(std::move(new_session));
  host_to_sessions_[host.get()].emplace(new_session_ptr);
  return new_session_ptr;
}

void UdpProxyFilter::ClusterInfo::removeSession(const ActiveSession* session) {
  // First remove from the host to sessions map.
  ASSERT(host_to_sessions_[&session->host()].count(session) == 1);
  auto host_sessions_it = host_to_sessions_.find(&session->host());
  host_sessions_it->second.erase(session);
  if (host_sessions_it->second.empty()) {
    host_to_sessions_.erase(host_sessions_it);
  }

  // Now remove it from the primary map.
  ASSERT(sessions_.count(session) == 1);
  sessions_.erase(session);
}

UdpProxyFilter::ActiveSession::ActiveSession(ClusterInfo& cluster,
                                             Network::UdpRecvData::LocalPeerAddresses&& addresses,
                                             const Upstream::HostConstSharedPtr& host)
    : cluster_(cluster), addresses_(std::move(addresses)), host_(host),
      idle_timer_(cluster.filter_.read_callbacks_->udpListener().dispatcher().createTimer(
          [this] { onIdleTimer(); })),
      // NOTE: The socket call can only fail due to memory/fd exhaustion. No local ephemeral port
      //       is bound until the first packet is sent to the upstream host.
      io_handle_(cluster.filter_.createIoHandle(host)),
      socket_event_(cluster.filter_.read_callbacks_->udpListener().dispatcher().createFileEvent(
          io_handle_->fd(), [this](uint32_t) { onReadReady(); }, Event::FileTriggerType::Edge,
          Event::FileReadyType::Read)) {
  ENVOY_LOG(debug, "creating new session: downstream={} local={} upstream={}",
            addresses_.peer_->asStringView(), addresses_.local_->asStringView(),
            host->address()->asStringView());
  cluster_.filter_.config_->stats().downstream_sess_total_.inc();
  cluster_.filter_.config_->stats().downstream_sess_active_.inc();
  cluster_.cluster_.info()
      ->resourceManager(Upstream::ResourcePriority::Default)
      .connections()
      .inc();

  // TODO(mattklein123): Enable dropped packets socket option. In general the Socket abstraction
  // does not work well right now for client sockets. It's too heavy weight and is aimed at listener
  // sockets. We need to figure out how to either refactor Socket into something that works better
  // for this use case or allow the socket option abstractions to work directly against an IO
  // handle.
}

UdpProxyFilter::ActiveSession::~ActiveSession() {
  cluster_.filter_.config_->stats().downstream_sess_active_.dec();
  cluster_.cluster_.info()
      ->resourceManager(Upstream::ResourcePriority::Default)
      .connections()
      .dec();
}

void UdpProxyFilter::ActiveSession::onIdleTimer() {
  ENVOY_LOG(debug, "session idle timeout: downstream={} local={}", addresses_.peer_->asStringView(),
            addresses_.local_->asStringView());
  cluster_.filter_.config_->stats().idle_timeout_.inc();
  cluster_.removeSession(this);
}

void UdpProxyFilter::ActiveSession::onReadReady() {
  idle_timer_->enableTimer(cluster_.filter_.config_->sessionTimeout());

  // TODO(mattklein123): We should not be passing *addresses_.local_ to this function as we are
  //                     not trying to populate the local address for received packets.
  uint32_t packets_dropped = 0;
  const Api::IoErrorPtr result = Network::Utility::readPacketsFromSocket(
      *io_handle_, *addresses_.local_, *this, cluster_.filter_.config_->timeSource(),
      packets_dropped);
  // TODO(mattklein123): Handle no error when we limit the number of packets read.
  if (result->getErrorCode() != Api::IoError::IoErrorCode::Again) {
    cluster_.cluster_stats_.sess_rx_errors_.inc();
  }
}

void UdpProxyFilter::ActiveSession::write(const Buffer::Instance& buffer) {
  ENVOY_LOG(trace, "writing {} byte datagram upstream: downstream={} local={} upstream={}",
            buffer.length(), addresses_.peer_->asStringView(), addresses_.local_->asStringView(),
            host_->address()->asStringView());
  const uint64_t buffer_length = buffer.length();
  cluster_.filter_.config_->stats().downstream_sess_rx_bytes_.add(buffer_length);
  cluster_.filter_.config_->stats().downstream_sess_rx_datagrams_.inc();

  idle_timer_->enableTimer(cluster_.filter_.config_->sessionTimeout());

  // NOTE: On the first write, a local ephemeral port is bound, and thus this write can fail due to
  //       port exhaustion.
  // NOTE: We do not specify the local IP to use for the sendmsg call. We allow the OS to select
  //       the right IP based on outbound routing rules.
  Api::IoCallUint64Result rc =
      Network::Utility::writeToSocket(*io_handle_, buffer, nullptr, *host_->address());
  if (!rc.ok()) {
    cluster_.cluster_stats_.sess_tx_errors_.inc();
  } else {
    cluster_.cluster_stats_.sess_tx_datagrams_.inc();
    cluster_.cluster_.info()->stats().upstream_cx_tx_bytes_total_.add(buffer_length);
  }
}

void UdpProxyFilter::ActiveSession::processPacket(Network::Address::InstanceConstSharedPtr,
                                                  Network::Address::InstanceConstSharedPtr,
                                                  Buffer::InstancePtr buffer, MonotonicTime) {
  ENVOY_LOG(trace, "writing {} byte datagram downstream: downstream={} local={} upstream={}",
            buffer->length(), addresses_.peer_->asStringView(), addresses_.local_->asStringView(),
            host_->address()->asStringView());
  const uint64_t buffer_length = buffer->length();

  cluster_.cluster_stats_.sess_rx_datagrams_.inc();
  cluster_.cluster_.info()->stats().upstream_cx_rx_bytes_total_.add(buffer_length);

  Network::UdpSendData data{addresses_.local_->ip(), *addresses_.peer_, *buffer};
  const Api::IoCallUint64Result rc = cluster_.filter_.read_callbacks_->udpListener().send(data);
  if (!rc.ok()) {
    cluster_.filter_.config_->stats().downstream_sess_tx_errors_.inc();
  } else {
    cluster_.filter_.config_->stats().downstream_sess_tx_bytes_.add(buffer_length);
    cluster_.filter_.config_->stats().downstream_sess_tx_datagrams_.inc();
  }
}

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
