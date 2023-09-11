#include "source/extensions/filters/udp/udp_proxy/udp_proxy_filter.h"

#include "envoy/network/listener.h"

#include "source/common/network/socket_option_factory.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {

UdpProxyFilter::UdpProxyFilter(Network::UdpReadFilterCallbacks& callbacks,
                               const UdpProxyFilterConfigSharedPtr& config)
    : UdpListenerReadFilter(callbacks), config_(config),
      cluster_update_callbacks_(
          config->clusterManager().addThreadLocalClusterUpdateCallbacks(*this)) {
  for (const auto& entry : config_->allClusterNames()) {
    Upstream::ThreadLocalCluster* cluster = config->clusterManager().getThreadLocalCluster(entry);
    if (cluster != nullptr) {
      Upstream::ThreadLocalClusterCommand command = [&cluster]() -> Upstream::ThreadLocalCluster& {
        return *cluster;
      };
      onClusterAddOrUpdate(cluster->info()->name(), command);
    }
  }

  if (!config_->proxyAccessLogs().empty()) {
    udp_proxy_stats_.emplace(StreamInfo::StreamInfoImpl(config_->timeSource(), nullptr));
  }
}

UdpProxyFilter::~UdpProxyFilter() {
  if (!config_->proxyAccessLogs().empty()) {
    fillProxyStreamInfo();
    for (const auto& access_log : config_->proxyAccessLogs()) {
      access_log->log(nullptr, nullptr, nullptr, udp_proxy_stats_.value(),
                      AccessLog::AccessLogType::NotSet);
    }
  }
}

void UdpProxyFilter::onClusterAddOrUpdate(absl::string_view cluster_name,
                                          Upstream::ThreadLocalClusterCommand& get_cluster) {
  ENVOY_LOG(debug, "udp proxy: attaching to cluster {}", cluster_name);

  auto& cluster = get_cluster();
  ASSERT((!cluster_infos_.contains(cluster_name)) ||
         &cluster_infos_[cluster_name]->cluster_ != &cluster);

  if (config_->usingPerPacketLoadBalancing()) {
    cluster_infos_.emplace(cluster_name,
                           std::make_unique<PerPacketLoadBalancingClusterInfo>(*this, cluster));
  } else {
    cluster_infos_.emplace(cluster_name,
                           std::make_unique<StickySessionClusterInfo>(*this, cluster));
  }
}

void UdpProxyFilter::onClusterRemoval(const std::string& cluster) {
  if (!cluster_infos_.contains(cluster)) {
    return;
  }

  ENVOY_LOG(debug, "udp proxy: detaching from cluster {}", cluster);
  cluster_infos_.erase(cluster);
}

Network::FilterStatus UdpProxyFilter::onData(Network::UdpRecvData& data) {
  const std::string& route = config_->route(*data.addresses_.local_, *data.addresses_.peer_);
  if (!cluster_infos_.contains(route)) {
    config_->stats().downstream_sess_no_route_.inc();
    return Network::FilterStatus::StopIteration;
  }

  return cluster_infos_[route]->onData(data);
}

Network::FilterStatus UdpProxyFilter::onReceiveError(Api::IoError::IoErrorCode) {
  config_->stats().downstream_sess_rx_errors_.inc();

  return Network::FilterStatus::StopIteration;
}

UdpProxyFilter::ClusterInfo::ClusterInfo(UdpProxyFilter& filter,
                                         Upstream::ThreadLocalCluster& cluster,
                                         SessionStorageType&& sessions)
    : filter_(filter), cluster_(cluster),
      cluster_stats_(generateStats(cluster.info()->statsScope())), sessions_(std::move(sessions)),
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
  // Sanity check the session accounting. This is not as fast as a straight teardown, but this is
  // not a performance critical path.
  while (!sessions_.empty()) {
    removeSession(sessions_.begin()->get());
  }
  ASSERT(host_to_sessions_.empty());
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

UdpProxyFilter::ActiveSession*
UdpProxyFilter::ClusterInfo::createSession(Network::UdpRecvData::LocalPeerAddresses&& addresses,
                                           const Upstream::HostConstSharedPtr& optional_host) {
  if (!cluster_.info()
           ->resourceManager(Upstream::ResourcePriority::Default)
           .connections()
           .canCreate()) {
    ENVOY_LOG(debug, "cannot create new connection.");
    cluster_.info()->trafficStats()->upstream_cx_overflow_.inc();
    return nullptr;
  }

  if (optional_host) {
    return createSessionWithHost(std::move(addresses), optional_host);
  }

  auto host = chooseHost(addresses.peer_);
  if (host == nullptr) {
    ENVOY_LOG(debug, "cannot find any valid host.");
    cluster_.info()->trafficStats()->upstream_cx_none_healthy_.inc();
    return nullptr;
  }
  return createSessionWithHost(std::move(addresses), host);
}

UdpProxyFilter::ActiveSession* UdpProxyFilter::ClusterInfo::createSessionWithHost(
    Network::UdpRecvData::LocalPeerAddresses&& addresses,
    const Upstream::HostConstSharedPtr& host) {
  ASSERT(host);
  auto new_session = std::make_unique<ActiveSession>(*this, std::move(addresses), host);
  new_session->createFilterChain();
  new_session->onNewSession();
  auto new_session_ptr = new_session.get();
  sessions_.emplace(std::move(new_session));
  host_to_sessions_[host.get()].emplace(new_session_ptr);
  return new_session_ptr;
}

Upstream::HostConstSharedPtr UdpProxyFilter::ClusterInfo::chooseHost(
    const Network::Address::InstanceConstSharedPtr& peer_address) const {
  UdpLoadBalancerContext context(filter_.config_->hashPolicy(), peer_address);
  Upstream::HostConstSharedPtr host = cluster_.loadBalancer().chooseHost(&context);
  return host;
}

UdpProxyFilter::StickySessionClusterInfo::StickySessionClusterInfo(
    UdpProxyFilter& filter, Upstream::ThreadLocalCluster& cluster)
    : ClusterInfo(filter, cluster,
                  SessionStorageType(1, HeterogeneousActiveSessionHash(false),
                                     HeterogeneousActiveSessionEqual(false))) {}

Network::FilterStatus UdpProxyFilter::StickySessionClusterInfo::onData(Network::UdpRecvData& data) {
  const auto active_session_it = sessions_.find(data.addresses_);
  ActiveSession* active_session;
  if (active_session_it == sessions_.end()) {
    active_session = createSession(std::move(data.addresses_));
    if (active_session == nullptr) {
      return Network::FilterStatus::StopIteration;
    }
  } else {
    active_session = active_session_it->get();
    if (active_session->host().coarseHealth() == Upstream::Host::Health::Unhealthy) {
      // If a host becomes unhealthy, we optimally would like to replace it with a new session
      // to a healthy host. We may eventually want to make this behavior configurable, but for now
      // this will be the universal behavior.
      auto host = chooseHost(data.addresses_.peer_);
      if (host != nullptr && host->coarseHealth() != Upstream::Host::Health::Unhealthy &&
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

  active_session->onData(data);

  return Network::FilterStatus::StopIteration;
}

UdpProxyFilter::PerPacketLoadBalancingClusterInfo::PerPacketLoadBalancingClusterInfo(
    UdpProxyFilter& filter, Upstream::ThreadLocalCluster& cluster)
    : ClusterInfo(filter, cluster,
                  SessionStorageType(1, HeterogeneousActiveSessionHash(true),
                                     HeterogeneousActiveSessionEqual(true))) {}

Network::FilterStatus
UdpProxyFilter::PerPacketLoadBalancingClusterInfo::onData(Network::UdpRecvData& data) {
  auto host = chooseHost(data.addresses_.peer_);
  if (host == nullptr) {
    ENVOY_LOG(debug, "cannot find any valid host.");
    cluster_.info()->trafficStats()->upstream_cx_none_healthy_.inc();
    return Network::FilterStatus::StopIteration;
  }

  ENVOY_LOG(debug, "selected {} host as upstream.", host->address()->asStringView());

  LocalPeerHostAddresses key{data.addresses_, *host};
  const auto active_session_it = sessions_.find(key);
  ActiveSession* active_session;
  if (active_session_it == sessions_.end()) {
    active_session = createSession(std::move(data.addresses_), host);
    if (active_session == nullptr) {
      return Network::FilterStatus::StopIteration;
    }
  } else {
    active_session = active_session_it->get();
    ENVOY_LOG(trace, "found already existing session on host {}.",
              active_session->host().address()->asStringView());
  }

  active_session->onData(data);

  return Network::FilterStatus::StopIteration;
}

std::atomic<uint64_t> UdpProxyFilter::ActiveSession::next_global_session_id_;

UdpProxyFilter::ActiveSession::ActiveSession(ClusterInfo& cluster,
                                             Network::UdpRecvData::LocalPeerAddresses&& addresses,
                                             const Upstream::HostConstSharedPtr& host)
    : cluster_(cluster), use_original_src_ip_(cluster_.filter_.config_->usingOriginalSrcIp()),
      addresses_(std::move(addresses)), host_(host),
      idle_timer_(cluster.filter_.read_callbacks_->udpListener().dispatcher().createTimer(
          [this] { onIdleTimer(); })),
      // NOTE: The socket call can only fail due to memory/fd exhaustion. No local ephemeral port
      //       is bound until the first packet is sent to the upstream host.
      socket_(cluster.filter_.createSocket(host)),
      udp_session_info_(
          StreamInfo::StreamInfoImpl(cluster_.filter_.config_->timeSource(), nullptr)),
      session_id_(next_global_session_id_++) {

  socket_->ioHandle().initializeFileEvent(
      cluster.filter_.read_callbacks_->udpListener().dispatcher(),
      [this](uint32_t) { onReadReady(); }, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Read);

  ENVOY_LOG(debug, "creating new session: downstream={} local={} upstream={}",
            addresses_.peer_->asStringView(), addresses_.local_->asStringView(),
            host->address()->asStringView());

  cluster_.filter_.config_->stats().downstream_sess_total_.inc();
  cluster_.filter_.config_->stats().downstream_sess_active_.inc();
  cluster_.cluster_.info()
      ->resourceManager(Upstream::ResourcePriority::Default)
      .connections()
      .inc();

  if (use_original_src_ip_) {
    const Network::Socket::OptionsSharedPtr socket_options =
        Network::SocketOptionFactory::buildIpTransparentOptions();
    const bool ok = Network::Socket::applyOptions(
        socket_options, *socket_, envoy::config::core::v3::SocketOption::STATE_PREBIND);

    RELEASE_ASSERT(ok, "Should never occur!");
    ENVOY_LOG(debug, "The original src is enabled for address {}.",
              addresses_.peer_->asStringView());
  }

  // TODO(mattklein123): Enable dropped packets socket option. In general the Socket abstraction
  // does not work well right now for client sockets. It's too heavy weight and is aimed at listener
  // sockets. We need to figure out how to either refactor Socket into something that works better
  // for this use case or allow the socket option abstractions to work directly against an IO
  // handle.
}

UdpProxyFilter::ActiveSession::~ActiveSession() {
  ENVOY_LOG(debug, "deleting the session: downstream={} local={} upstream={}",
            addresses_.peer_->asStringView(), addresses_.local_->asStringView(),
            host_->address()->asStringView());
  cluster_.filter_.config_->stats().downstream_sess_active_.dec();
  cluster_.cluster_.info()
      ->resourceManager(Upstream::ResourcePriority::Default)
      .connections()
      .dec();

  if (!cluster_.filter_.config_->sessionAccessLogs().empty()) {
    fillSessionStreamInfo();
    for (const auto& access_log : cluster_.filter_.config_->sessionAccessLogs()) {
      access_log->log(nullptr, nullptr, nullptr, udp_session_info_,
                      AccessLog::AccessLogType::NotSet);
    }
  }
}

void UdpProxyFilter::ActiveSession::fillSessionStreamInfo() {
  ProtobufWkt::Struct stats_obj;
  auto& fields_map = *stats_obj.mutable_fields();
  fields_map["cluster_name"] = ValueUtil::stringValue(cluster_.cluster_.info()->name());
  fields_map["bytes_sent"] = ValueUtil::numberValue(session_stats_.downstream_sess_tx_bytes_);
  fields_map["bytes_received"] = ValueUtil::numberValue(session_stats_.downstream_sess_rx_bytes_);
  fields_map["errors_sent"] = ValueUtil::numberValue(session_stats_.downstream_sess_tx_errors_);
  fields_map["datagrams_sent"] =
      ValueUtil::numberValue(session_stats_.downstream_sess_tx_datagrams_);
  fields_map["datagrams_received"] =
      ValueUtil::numberValue(session_stats_.downstream_sess_rx_datagrams_);

  udp_session_info_.setDynamicMetadata("udp.proxy.session", stats_obj);
}

void UdpProxyFilter::fillProxyStreamInfo() {
  ProtobufWkt::Struct stats_obj;
  auto& fields_map = *stats_obj.mutable_fields();
  fields_map["bytes_sent"] =
      ValueUtil::numberValue(config_->stats().downstream_sess_tx_bytes_.value());
  fields_map["bytes_received"] =
      ValueUtil::numberValue(config_->stats().downstream_sess_rx_bytes_.value());
  fields_map["errors_sent"] =
      ValueUtil::numberValue(config_->stats().downstream_sess_tx_errors_.value());
  fields_map["errors_received"] =
      ValueUtil::numberValue(config_->stats().downstream_sess_rx_errors_.value());
  fields_map["datagrams_sent"] =
      ValueUtil::numberValue(config_->stats().downstream_sess_tx_datagrams_.value());
  fields_map["datagrams_received"] =
      ValueUtil::numberValue(config_->stats().downstream_sess_rx_datagrams_.value());
  fields_map["no_route"] =
      ValueUtil::numberValue(config_->stats().downstream_sess_no_route_.value());
  fields_map["session_total"] =
      ValueUtil::numberValue(config_->stats().downstream_sess_total_.value());
  fields_map["idle_timeout"] = ValueUtil::numberValue(config_->stats().idle_timeout_.value());

  udp_proxy_stats_.value().setDynamicMetadata("udp.proxy.proxy", stats_obj);
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
      socket_->ioHandle(), *addresses_.local_, *this, cluster_.filter_.config_->timeSource(),
      cluster_.filter_.config_->upstreamSocketConfig().prefer_gro_, packets_dropped);
  if (result == nullptr) {
    socket_->ioHandle().activateFileEvents(Event::FileReadyType::Read);
    return;
  }
  if (result->getErrorCode() != Api::IoError::IoErrorCode::Again) {
    cluster_.cluster_stats_.sess_rx_errors_.inc();
  }
  // Flush out buffered data at the end of IO event.
  cluster_.filter_.read_callbacks_->udpListener().flush();
}

void UdpProxyFilter::ActiveSession::onNewSession() {
  for (auto& active_read_filter : read_filters_) {
    if (active_read_filter->initialized_) {
      // The filter may call continueFilterChain() in onNewSession(), causing next
      // filters to iterate onNewSession(), so check that it was not called before.
      continue;
    }

    active_read_filter->initialized_ = true;
    auto status = active_read_filter->read_filter_->onNewSession();
    if (status == ReadFilterStatus::StopIteration) {
      return;
    }
  }
}

void UdpProxyFilter::ActiveSession::onData(Network::UdpRecvData& data) {
  ENVOY_LOG(trace, "received {} byte datagram from downstream: downstream={} local={} upstream={}",
            data.buffer_->length(), addresses_.peer_->asStringView(),
            addresses_.local_->asStringView(), host_->address()->asStringView());

  const uint64_t rx_buffer_length = data.buffer_->length();
  cluster_.filter_.config_->stats().downstream_sess_rx_bytes_.add(rx_buffer_length);
  session_stats_.downstream_sess_rx_bytes_ += rx_buffer_length;
  cluster_.filter_.config_->stats().downstream_sess_rx_datagrams_.inc();
  ++session_stats_.downstream_sess_rx_datagrams_;

  idle_timer_->enableTimer(cluster_.filter_.config_->sessionTimeout());

  // NOTE: On the first write, a local ephemeral port is bound, and thus this write can fail due to
  //       port exhaustion. To avoid exhaustion, UDP sockets will be connected and associated with
  //       a 4-tuple including the local IP, and the UDP port may be reused for multiple
  //       connections unless use_original_src_ip_ is set. When use_original_src_ip_ is set, the
  //       socket should not be connected since the source IP will be changed.
  // NOTE: We do not specify the local IP to use for the sendmsg call if use_original_src_ip_ is not
  //       set. We allow the OS to select the right IP based on outbound routing rules if
  //       use_original_src_ip_ is not set, else use downstream peer IP as local IP.
  if (!use_original_src_ip_ && !connected_) {
    Api::SysCallIntResult rc = socket_->ioHandle().connect(host_->address());
    if (SOCKET_FAILURE(rc.return_value_)) {
      ENVOY_LOG(debug, "cannot connect: ({}) {}", rc.errno_, errorDetails(rc.errno_));
      cluster_.cluster_stats_.sess_tx_errors_.inc();
      return;
    }

    connected_ = true;
  }

  for (auto& active_read_filter : read_filters_) {
    auto status = active_read_filter->read_filter_->onData(data);
    if (status == ReadFilterStatus::StopIteration) {
      return;
    }
  }

  writeUpstream(data);
}

void UdpProxyFilter::ActiveSession::writeUpstream(Network::UdpRecvData& data) {
  ASSERT(connected_ || use_original_src_ip_);

  const uint64_t tx_buffer_length = data.buffer_->length();
  ENVOY_LOG(trace, "writing {} byte datagram upstream: downstream={} local={} upstream={}",
            tx_buffer_length, addresses_.peer_->asStringView(), addresses_.local_->asStringView(),
            host_->address()->asStringView());

  const Network::Address::Ip* local_ip = use_original_src_ip_ ? addresses_.peer_->ip() : nullptr;
  Api::IoCallUint64Result rc = Network::Utility::writeToSocket(socket_->ioHandle(), *data.buffer_,
                                                               local_ip, *host_->address());

  if (!rc.ok()) {
    cluster_.cluster_stats_.sess_tx_errors_.inc();
  } else {
    cluster_.cluster_stats_.sess_tx_datagrams_.inc();
    cluster_.cluster_.info()->trafficStats()->upstream_cx_tx_bytes_total_.add(tx_buffer_length);
  }
}

void UdpProxyFilter::ActiveSession::onContinueFilterChain(ActiveReadFilter* filter) {
  ASSERT(filter != nullptr);

  std::list<ActiveReadFilterPtr>::iterator entry = std::next(filter->entry());
  for (; entry != read_filters_.end(); entry++) {
    if (!(*entry)->read_filter_ || (*entry)->initialized_) {
      continue;
    }

    (*entry)->initialized_ = true;
    auto status = (*entry)->read_filter_->onNewSession();
    if (status == ReadFilterStatus::StopIteration) {
      break;
    }
  }
}

void UdpProxyFilter::ActiveSession::onInjectReadDatagramToFilterChain(ActiveReadFilter* filter,
                                                                      Network::UdpRecvData& data) {
  ASSERT(filter != nullptr);

  std::list<ActiveReadFilterPtr>::iterator entry = std::next(filter->entry());
  for (; entry != read_filters_.end(); entry++) {
    if (!(*entry)->read_filter_) {
      continue;
    }

    auto status = (*entry)->read_filter_->onData(data);
    if (status == ReadFilterStatus::StopIteration) {
      return;
    }
  }

  writeUpstream(data);
}

void UdpProxyFilter::ActiveSession::onInjectWriteDatagramToFilterChain(ActiveWriteFilter* filter,
                                                                       Network::UdpRecvData& data) {
  ASSERT(filter != nullptr);

  std::list<ActiveWriteFilterPtr>::iterator entry = std::next(filter->entry());
  for (; entry != write_filters_.end(); entry++) {
    if (!(*entry)->write_filter_) {
      continue;
    }

    auto status = (*entry)->write_filter_->onWrite(data);
    if (status == WriteFilterStatus::StopIteration) {
      return;
    }
  }

  writeDownstream(data);
}

void UdpProxyFilter::ActiveSession::processPacket(
    Network::Address::InstanceConstSharedPtr local_address,
    Network::Address::InstanceConstSharedPtr peer_address, Buffer::InstancePtr buffer,
    MonotonicTime receive_time) {
  const uint64_t rx_buffer_length = buffer->length();
  ENVOY_LOG(trace, "received {} byte datagram from upstream: downstream={} local={} upstream={}",
            rx_buffer_length, addresses_.peer_->asStringView(), addresses_.local_->asStringView(),
            host_->address()->asStringView());

  cluster_.cluster_stats_.sess_rx_datagrams_.inc();
  cluster_.cluster_.info()->trafficStats()->upstream_cx_rx_bytes_total_.add(rx_buffer_length);

  Network::UdpRecvData recv_data{
      {std::move(local_address), std::move(peer_address)}, std::move(buffer), receive_time};
  for (auto& active_write_filter : write_filters_) {
    auto status = active_write_filter->write_filter_->onWrite(recv_data);
    if (status == WriteFilterStatus::StopIteration) {
      return;
    }
  }

  writeDownstream(recv_data);
}

void UdpProxyFilter::ActiveSession::writeDownstream(Network::UdpRecvData& recv_data) {
  const uint64_t tx_buffer_length = recv_data.buffer_->length();
  ENVOY_LOG(trace, "writing {} byte datagram downstream: downstream={} local={} upstream={}",
            tx_buffer_length, addresses_.peer_->asStringView(), addresses_.local_->asStringView(),
            host_->address()->asStringView());

  Network::UdpSendData data{addresses_.local_->ip(), *addresses_.peer_, *recv_data.buffer_};
  const Api::IoCallUint64Result rc = cluster_.filter_.read_callbacks_->udpListener().send(data);
  if (!rc.ok()) {
    cluster_.filter_.config_->stats().downstream_sess_tx_errors_.inc();
    ++session_stats_.downstream_sess_tx_errors_;
  } else {
    cluster_.filter_.config_->stats().downstream_sess_tx_bytes_.add(tx_buffer_length);
    session_stats_.downstream_sess_tx_bytes_ += tx_buffer_length;
    cluster_.filter_.config_->stats().downstream_sess_tx_datagrams_.inc();
    ++session_stats_.downstream_sess_tx_datagrams_;
  }
}

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
