#include "source/extensions/filters/udp/udp_proxy/udp_proxy_filter.h"

#include "envoy/network/listener.h"

#include "source/common/buffer/buffer_impl.h"
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
    udp_proxy_stats_.emplace(StreamInfo::StreamInfoImpl(
        config_->timeSource(), nullptr, StreamInfo::FilterState::LifeSpan::Connection));
  }
}

UdpProxyFilter::~UdpProxyFilter() {
  if (!config_->proxyAccessLogs().empty()) {
    fillProxyStreamInfo();
    for (const auto& access_log : config_->proxyAccessLogs()) {
      access_log->log({}, udp_proxy_stats_.value());
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
    cluster_infos_.insert_or_assign(
        cluster_name, std::make_unique<PerPacketLoadBalancingClusterInfo>(*this, cluster));
  } else {
    cluster_infos_.insert_or_assign(cluster_name,
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
  if (udp_proxy_stats_) {
    udp_proxy_stats_.value().addBytesReceived(data.buffer_->length());
  }

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
                for (auto& session : host_sessions_it->second) {
                  session->onSessionComplete();
                  ASSERT(sessions_.count(session) == 1);
                  sessions_.erase(session);
                }
                host_to_sessions_.erase(host_sessions_it);
              }
            }
            return absl::OkStatus();
          })) {}

UdpProxyFilter::ClusterInfo::~ClusterInfo() {
  // Sanity check the session accounting. This is not as fast as a straight teardown, but this is
  // not a performance critical path.
  while (!sessions_.empty()) {
    removeSession(sessions_.begin()->get());
  }
  ASSERT(host_to_sessions_.empty());
}

void UdpProxyFilter::ClusterInfo::removeSession(ActiveSession* session) {
  if (session->host().has_value()) {
    // First remove from the host to sessions map, in case the host was resolved.
    ASSERT(host_to_sessions_[&session->host().value().get()].count(session) == 1);
    auto host_sessions_it = host_to_sessions_.find(&session->host().value().get());
    host_sessions_it->second.erase(session);
    if (host_sessions_it->second.empty()) {
      host_to_sessions_.erase(host_sessions_it);
    }
  }

  session->onSessionComplete();

  // Now remove it from the primary map.
  ASSERT(sessions_.count(session) == 1);
  sessions_.erase(session);
}

UdpProxyFilter::ActiveSession*
UdpProxyFilter::ClusterInfo::createSession(Network::UdpRecvData::LocalPeerAddresses&& addresses,
                                           const Upstream::HostConstSharedPtr& optional_host,
                                           bool defer_socket_creation) {
  if (!cluster_.info()
           ->resourceManager(Upstream::ResourcePriority::Default)
           .connections()
           .canCreate()) {
    ENVOY_LOG(debug, "cannot create new connection.");
    cluster_.info()->trafficStats()->upstream_cx_overflow_.inc();
    return nullptr;
  }

  if (defer_socket_creation) {
    ASSERT(!optional_host);
    return createSessionWithOptionalHost(std::move(addresses), nullptr);
  }

  if (optional_host) {
    return createSessionWithOptionalHost(std::move(addresses), optional_host);
  }

  auto host = chooseHost(addresses.peer_, nullptr);
  if (host == nullptr) {
    ENVOY_LOG(debug, "cannot find any valid host.");
    cluster_.info()->trafficStats()->upstream_cx_none_healthy_.inc();
    return nullptr;
  }

  return createSessionWithOptionalHost(std::move(addresses), host);
}

UdpProxyFilter::ActiveSession* UdpProxyFilter::ClusterInfo::createSessionWithOptionalHost(
    Network::UdpRecvData::LocalPeerAddresses&& addresses,
    const Upstream::HostConstSharedPtr& host) {
  ActiveSessionPtr new_session;
  if (filter_.config_->tunnelingConfig()) {
    ASSERT(!host);
    new_session = std::make_unique<TunnelingActiveSession>(*this, std::move(addresses));
  } else {
    new_session = std::make_unique<UdpActiveSession>(*this, std::move(addresses), host);
  }

  if (!new_session->createFilterChain()) {
    filter_.config_->stats().session_filter_config_missing_.inc();
    new_session->onSessionComplete();
    return nullptr;
  }

  if (new_session->onNewSession()) {
    auto new_session_ptr = new_session.get();
    sessions_.emplace(std::move(new_session));
    return new_session_ptr;
  }

  new_session->onSessionComplete();
  return nullptr;
}

Upstream::HostConstSharedPtr UdpProxyFilter::ClusterInfo::chooseHost(
    const Network::Address::InstanceConstSharedPtr& peer_address,
    const StreamInfo::StreamInfo* stream_info) const {
  UdpLoadBalancerContext context(filter_.config_->hashPolicy(), peer_address, stream_info);
  Upstream::HostConstSharedPtr host = cluster_.loadBalancer().chooseHost(&context);
  return host;
}

UdpProxyFilter::StickySessionClusterInfo::StickySessionClusterInfo(
    UdpProxyFilter& filter, Upstream::ThreadLocalCluster& cluster)
    : ClusterInfo(filter, cluster,
                  SessionStorageType(1, HeterogeneousActiveSessionHash(false),
                                     HeterogeneousActiveSessionEqual(false))) {}

Network::FilterStatus UdpProxyFilter::StickySessionClusterInfo::onData(Network::UdpRecvData& data) {
  bool defer_socket = filter_.config_->hasSessionFilters() || filter_.config_->tunnelingConfig();
  const auto active_session_it = sessions_.find(data.addresses_);
  ActiveSession* active_session;
  if (active_session_it == sessions_.end()) {
    active_session = createSession(std::move(data.addresses_), nullptr, defer_socket);
    if (active_session == nullptr) {
      return Network::FilterStatus::StopIteration;
    }
  } else {
    active_session = active_session_it->get();
    // We defer the socket creation when the session includes filters, so the filters can be
    // iterated before choosing the host, to allow dynamically choosing upstream host. Due to this,
    // we can't perform health checks during a session.
    // TODO(ohadvano): add similar functionality that is performed after session filter chain
    // iteration to signal filters about the unhealthy host, or replace the host during the session.
    if (!defer_socket &&
        active_session->host().value().get().coarseHealth() == Upstream::Host::Health::Unhealthy) {
      // If a host becomes unhealthy, we optimally would like to replace it with a new session
      // to a healthy host. We may eventually want to make this behavior configurable, but for now
      // this will be the universal behavior.
      auto host = chooseHost(data.addresses_.peer_, nullptr);
      if (host != nullptr && host->coarseHealth() != Upstream::Host::Health::Unhealthy &&
          host.get() != &active_session->host().value().get()) {
        ENVOY_LOG(debug, "upstream session unhealthy, recreating the session");
        removeSession(active_session);
        active_session = createSession(std::move(data.addresses_), host, false);
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
  auto host = chooseHost(data.addresses_.peer_, nullptr);
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
    active_session = createSession(std::move(data.addresses_), host, false);
    if (active_session == nullptr) {
      return Network::FilterStatus::StopIteration;
    }
  } else {
    active_session = active_session_it->get();
    ENVOY_LOG(trace, "found already existing session on host {}.",
              active_session->host().value().get().address()->asStringView());
  }

  active_session->onData(data);

  return Network::FilterStatus::StopIteration;
}

std::atomic<uint64_t> UdpProxyFilter::ActiveSession::next_global_session_id_;

UdpProxyFilter::ActiveSession::ActiveSession(ClusterInfo& cluster,
                                             Network::UdpRecvData::LocalPeerAddresses&& addresses,
                                             const Upstream::HostConstSharedPtr& host)
    : cluster_(cluster), addresses_(std::move(addresses)), host_(host),
      session_id_(next_global_session_id_++),
      idle_timer_(cluster.filter_.read_callbacks_->udpListener().dispatcher().createTimer(
          [this] { onIdleTimer(); })),
      udp_session_info_(StreamInfo::StreamInfoImpl(cluster_.filter_.config_->timeSource(),
                                                   createDownstreamConnectionInfoProvider(),
                                                   StreamInfo::FilterState::LifeSpan::Connection)) {
  udp_session_info_.setUpstreamInfo(std::make_shared<StreamInfo::UpstreamInfoImpl>());
  cluster_.filter_.config_->stats().downstream_sess_total_.inc();
  cluster_.filter_.config_->stats().downstream_sess_active_.inc();
  cluster_.cluster_.info()
      ->resourceManager(Upstream::ResourcePriority::Default)
      .connections()
      .inc();
}

UdpProxyFilter::UdpActiveSession::UdpActiveSession(
    ClusterInfo& cluster, Network::UdpRecvData::LocalPeerAddresses&& addresses,
    const Upstream::HostConstSharedPtr& host)
    : ActiveSession(cluster, std::move(addresses), std::move(host)),
      use_original_src_ip_(cluster.filter_.config_->usingOriginalSrcIp()) {}

UdpProxyFilter::ActiveSession::~ActiveSession() {
  ENVOY_BUG(on_session_complete_called_, "onSessionComplete() not called");
}

void UdpProxyFilter::ActiveSession::onSessionComplete() {
  ENVOY_LOG(debug, "deleting the session: downstream={} local={} upstream={}",
            addresses_.peer_->asStringView(), addresses_.local_->asStringView(),
            host_ != nullptr ? host_->address()->asStringView() : "unknown");

  cluster_.filter_.config_->stats().downstream_sess_active_.dec();
  cluster_.cluster_.info()
      ->resourceManager(Upstream::ResourcePriority::Default)
      .connections()
      .dec();

  disableAccessLogFlushTimer();

  for (auto& active_read_filter : read_filters_) {
    active_read_filter->read_filter_->onSessionComplete();
  }

  for (auto& active_write_filter : write_filters_) {
    active_write_filter->write_filter_->onSessionComplete();
  }

  if (!cluster_.filter_.config_->sessionAccessLogs().empty()) {
    fillSessionStreamInfo();
    const Formatter::HttpFormatterContext log_context{
        nullptr, nullptr, nullptr, {}, AccessLog::AccessLogType::UdpSessionEnd};
    for (const auto& access_log : cluster_.filter_.config_->sessionAccessLogs()) {
      access_log->log(log_context, udp_session_info_);
    }
  }

  on_session_complete_called_ = true;
}

std::shared_ptr<Network::ConnectionInfoSetterImpl>
UdpProxyFilter::ActiveSession::createDownstreamConnectionInfoProvider() {
  auto downstream_connection_info_provider =
      std::make_shared<Network::ConnectionInfoSetterImpl>(addresses_.local_, addresses_.peer_);
  downstream_connection_info_provider->setConnectionID(session_id_);
  return downstream_connection_info_provider;
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

void UdpProxyFilter::UdpActiveSession::onIdleTimer() {
  ENVOY_LOG(debug, "session idle timeout: downstream={} local={}", addresses_.peer_->asStringView(),
            addresses_.local_->asStringView());
  cluster_.filter_.config_->stats().idle_timeout_.inc();
  cluster_.removeSession(this);
}

void UdpProxyFilter::UdpActiveSession::onReadReady() {
  resetIdleTimer();

  // TODO(mattklein123): We should not be passing *addresses_.local_ to this function as we are
  //                     not trying to populate the local address for received packets.
  uint32_t packets_dropped = 0;
  const Api::IoErrorPtr result = Network::Utility::readPacketsFromSocket(
      udp_socket_->ioHandle(), *addresses_.local_, *this, cluster_.filter_.config_->timeSource(),
      cluster_.filter_.config_->upstreamSocketConfig().prefer_gro_, /*allow_mmsg=*/true,
      packets_dropped);
  if (result == nullptr) {
    udp_socket_->ioHandle().activateFileEvents(Event::FileReadyType::Read);
    return;
  }
  if (result->getErrorCode() != Api::IoError::IoErrorCode::Again) {
    cluster_.cluster_stats_.sess_rx_errors_.inc();
  }
  // Flush out buffered data at the end of IO event.
  cluster_.filter_.read_callbacks_->udpListener().flush();
}

bool UdpProxyFilter::ActiveSession::onNewSession() {
  if (cluster_.filter_.config_->accessLogFlushInterval().has_value() &&
      !cluster_.filter_.config_->sessionAccessLogs().empty()) {
    access_log_flush_timer_ =
        cluster_.filter_.read_callbacks_->udpListener().dispatcher().createTimer(
            [this] { onAccessLogFlushInterval(); });
    rearmAccessLogFlushTimer();
  }

  // Set UUID for the session. This is used for logging and tracing.
  udp_session_info_.setStreamIdProvider(std::make_shared<StreamInfo::StreamIdProviderImpl>(
      cluster_.filter_.config_->randomGenerator().uuid()));

  for (auto& active_read_filter : read_filters_) {
    if (active_read_filter->initialized_) {
      // The filter may call continueFilterChain() in onNewSession(), causing next
      // filters to iterate onNewSession(), so check that it was not called before.
      continue;
    }

    active_read_filter->initialized_ = true;
    auto status = active_read_filter->read_filter_->onNewSession();
    if (status == ReadFilterStatus::StopIteration) {
      return true;
    }
  }

  return createUpstream();
}

void UdpProxyFilter::ActiveSession::onData(Network::UdpRecvData& data) {
  const uint64_t rx_buffer_length = data.buffer_->length();
  ENVOY_LOG(trace, "received {} byte datagram from downstream: downstream={} local={} upstream={}",
            rx_buffer_length, addresses_.peer_->asStringView(), addresses_.local_->asStringView(),
            host_ != nullptr ? host_->address()->asStringView() : "unknown");

  streamInfo().addBytesReceived(rx_buffer_length);
  cluster_.filter_.config_->stats().downstream_sess_rx_bytes_.add(rx_buffer_length);
  session_stats_.downstream_sess_rx_bytes_ += rx_buffer_length;
  cluster_.filter_.config_->stats().downstream_sess_rx_datagrams_.inc();
  ++session_stats_.downstream_sess_rx_datagrams_;
  resetIdleTimer();

  for (auto& active_read_filter : read_filters_) {
    auto status = active_read_filter->read_filter_->onData(data);
    if (status == ReadFilterStatus::StopIteration) {
      return;
    }
  }

  writeUpstream(data);
}

void UdpProxyFilter::UdpActiveSession::writeUpstream(Network::UdpRecvData& data) {
  if (!udp_socket_) {
    ENVOY_LOG(debug, "cannot write upstream because the socket was not created.");
    return;
  }

  // NOTE: On the first write, a local ephemeral port is bound, and thus this write can fail due to
  //       port exhaustion. To avoid exhaustion, UDP sockets will be connected and associated with
  //       a 4-tuple including the local IP, and the UDP port may be reused for multiple
  //       connections unless use_original_src_ip_ is set. When use_original_src_ip_ is set, the
  //       socket should not be connected since the source IP will be changed.
  // NOTE: We do not specify the local IP to use for the sendmsg call if use_original_src_ip_ is not
  //       set. We allow the OS to select the right IP based on outbound routing rules if
  //       use_original_src_ip_ is not set, else use downstream peer IP as local IP.
  if (!connected_ && !use_original_src_ip_) {
    Api::SysCallIntResult rc = udp_socket_->ioHandle().connect(host_->address());
    if (SOCKET_FAILURE(rc.return_value_)) {
      ENVOY_LOG(debug, "cannot connect: ({}) {}", rc.errno_, errorDetails(rc.errno_));
      cluster_.cluster_stats_.sess_tx_errors_.inc();
      return;
    }

    connected_ = true;
  }

  ASSERT((connected_ || use_original_src_ip_) && udp_socket_ && host_);

  const uint64_t tx_buffer_length = data.buffer_->length();
  ENVOY_LOG(trace, "writing {} byte datagram upstream: downstream={} local={} upstream={}",
            tx_buffer_length, addresses_.peer_->asStringView(), addresses_.local_->asStringView(),
            host_->address()->asStringView());

  const Network::Address::Ip* local_ip = use_original_src_ip_ ? addresses_.peer_->ip() : nullptr;
  Api::IoCallUint64Result rc = Network::Utility::writeToSocket(
      udp_socket_->ioHandle(), *data.buffer_, local_ip, *host_->address());

  if (!rc.ok()) {
    cluster_.cluster_stats_.sess_tx_errors_.inc();
  } else {
    cluster_.cluster_stats_.sess_tx_datagrams_.inc();
    cluster_.cluster_.info()->trafficStats()->upstream_cx_tx_bytes_total_.add(tx_buffer_length);
  }
}

bool UdpProxyFilter::ActiveSession::onContinueFilterChain(ActiveReadFilter* filter) {
  ASSERT(filter != nullptr);

  std::list<ActiveReadFilterPtr>::iterator entry = std::next(filter->entry());
  for (; entry != read_filters_.end(); entry++) {
    if (!(*entry)->read_filter_ || (*entry)->initialized_) {
      continue;
    }

    (*entry)->initialized_ = true;
    auto status = (*entry)->read_filter_->onNewSession();
    if (status == ReadFilterStatus::StopIteration) {
      return true;
    }
  }

  if (!createUpstream()) {
    cluster_.removeSession(this);
    return false;
  }

  return true;
}

bool UdpProxyFilter::UdpActiveSession::createUpstream() {
  if (udp_socket_) {
    // A session filter may call on continueFilterChain(), after already creating the socket,
    // so we first check that the socket was not created already.
    return true;
  }

  if (!host_) {
    host_ = cluster_.chooseHost(addresses_.peer_, &udp_session_info_);
    if (host_ == nullptr) {
      ENVOY_LOG(debug, "cannot find any valid host.");
      cluster_.cluster_.info()->trafficStats()->upstream_cx_none_healthy_.inc();
      return false;
    }
  }

  udp_session_info_.upstreamInfo()->setUpstreamHost(host_);
  cluster_.addSession(host_.get(), this);
  createUdpSocket(host_);
  return true;
}

void UdpProxyFilter::UdpActiveSession::createUdpSocket(const Upstream::HostConstSharedPtr& host) {
  // NOTE: The socket call can only fail due to memory/fd exhaustion. No local ephemeral port
  //       is bound until the first packet is sent to the upstream host.
  udp_socket_ = cluster_.filter_.createUdpSocket(host);
  udp_socket_->ioHandle().initializeFileEvent(
      cluster_.filter_.read_callbacks_->udpListener().dispatcher(),
      [this](uint32_t) {
        onReadReady();
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  ENVOY_LOG(debug, "creating new session: downstream={} local={} upstream={}",
            addresses_.peer_->asStringView(), addresses_.local_->asStringView(),
            host->address()->asStringView());

  if (use_original_src_ip_) {
    const Network::Socket::OptionsSharedPtr socket_options =
        Network::SocketOptionFactory::buildIpTransparentOptions();
    const bool ok = Network::Socket::applyOptions(
        socket_options, *udp_socket_, envoy::config::core::v3::SocketOption::STATE_PREBIND);

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

void UdpProxyFilter::UdpActiveSession::processPacket(
    Network::Address::InstanceConstSharedPtr local_address,
    Network::Address::InstanceConstSharedPtr peer_address, Buffer::InstancePtr buffer,
    MonotonicTime receive_time, uint8_t tos, Buffer::RawSlice saved_cmsg) {
  const uint64_t rx_buffer_length = buffer->length();
  ENVOY_LOG(trace, "received {} byte datagram from upstream: downstream={} local={} upstream={}",
            rx_buffer_length, addresses_.peer_->asStringView(), addresses_.local_->asStringView(),
            host_ != nullptr ? host_->address()->asStringView() : "unknown");

  cluster_.cluster_stats_.sess_rx_datagrams_.inc();
  cluster_.cluster_.info()->trafficStats()->upstream_cx_rx_bytes_total_.add(rx_buffer_length);

  Network::UdpRecvData recv_data{{std::move(local_address), std::move(peer_address)},
                                 std::move(buffer),
                                 receive_time,
                                 tos,
                                 saved_cmsg};
  processUpstreamDatagram(recv_data);
}

void UdpProxyFilter::ActiveSession::resetIdleTimer() {
  if (idle_timer_ == nullptr) {
    return;
  }

  idle_timer_->enableTimer(cluster_.filter_.config_->sessionTimeout());
}

void UdpProxyFilter::ActiveSession::processUpstreamDatagram(Network::UdpRecvData& recv_data) {
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
            host_ != nullptr ? host_->address()->asStringView() : "unknown");

  streamInfo().addBytesSent(tx_buffer_length);

  if (cluster_.filter_.udp_proxy_stats_) {
    cluster_.filter_.udp_proxy_stats_.value().addBytesSent(tx_buffer_length);
  }

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

void UdpProxyFilter::ActiveSession::onAccessLogFlushInterval() {
  fillSessionStreamInfo();
  const Formatter::HttpFormatterContext log_context{
      nullptr, nullptr, nullptr, {}, AccessLog::AccessLogType::UdpPeriodic};
  for (const auto& access_log : cluster_.filter_.config_->sessionAccessLogs()) {
    access_log->log(log_context, udp_session_info_);
  }

  rearmAccessLogFlushTimer();
}

void UdpProxyFilter::ActiveSession::rearmAccessLogFlushTimer() {
  if (access_log_flush_timer_ != nullptr) {
    ASSERT(cluster_.filter_.config_->accessLogFlushInterval().has_value());
    access_log_flush_timer_->enableTimer(
        cluster_.filter_.config_->accessLogFlushInterval().value());
  }
}

void UdpProxyFilter::ActiveSession::disableAccessLogFlushTimer() {
  if (access_log_flush_timer_ != nullptr) {
    access_log_flush_timer_->disableTimer();
    access_log_flush_timer_.reset();
  }
}

void HttpUpstreamImpl::encodeData(Buffer::Instance& data) {
  if (!request_encoder_) {
    return;
  }

  request_encoder_->encodeData(data, false);
}

void HttpUpstreamImpl::setRequestEncoder(Http::RequestEncoder& request_encoder, bool is_ssl) {
  request_encoder_ = &request_encoder;
  request_encoder_->getStream().addCallbacks(*this);

  const std::string& scheme =
      is_ssl ? Http::Headers::get().SchemeValues.Https : Http::Headers::get().SchemeValues.Http;

  std::string host = tunnel_config_.proxyHost(downstream_info_);
  const auto* dynamic_port =
      downstream_info_.filterState()->getDataReadOnly<StreamInfo::UInt32Accessor>(
          "udp.connect.proxy_port");
  if (dynamic_port != nullptr && dynamic_port->value() > 0 && dynamic_port->value() <= 65535) {
    absl::StrAppend(&host, ":", std::to_string(dynamic_port->value()));
  } else if (tunnel_config_.proxyPort().has_value()) {
    absl::StrAppend(&host, ":", std::to_string(tunnel_config_.proxyPort().value()));
  }

  auto headers = Http::RequestHeaderMapImpl::create();
  headers->addReferenceKey(Http::Headers::get().Scheme, scheme);
  headers->addReferenceKey(Http::Headers::get().Host, host);

  if (tunnel_config_.usePost()) {
    headers->addReferenceKey(Http::Headers::get().Method, "POST");
    headers->addReferenceKey(Http::Headers::get().Path, tunnel_config_.postPath());
  } else {
    // The Envoy HTTP/2 and HTTP/3 clients expect the request header map to be in the form of HTTP/1
    // upgrade to issue an extended CONNECT request.
    headers->addReferenceKey(Http::Headers::get().Method, "GET");
    headers->addReferenceKey(Http::Headers::get().Connection, "Upgrade");
    headers->addReferenceKey(Http::Headers::get().Upgrade, "connect-udp");
    headers->addReferenceKey(Http::Headers::get().CapsuleProtocol, "?1");
    const std::string target_tunnel_path = resolveTargetTunnelPath();
    headers->addReferenceKey(Http::Headers::get().Path, target_tunnel_path);
  }

  tunnel_config_.headerEvaluator().evaluateHeaders(*headers, {downstream_info_.getRequestHeaders()},
                                                   downstream_info_);

  const auto status = request_encoder_->encodeHeaders(*headers, false);
  // Encoding can only fail on missing required request headers.
  ASSERT(status.ok());
}

const std::string HttpUpstreamImpl::resolveTargetTunnelPath() {
  std::string target_host = tunnel_config_.targetHost(downstream_info_);
  target_host = Http::Utility::PercentEncoding::encode(target_host, ":");

  const auto* dynamic_port =
      downstream_info_.filterState()->getDataReadOnly<StreamInfo::UInt32Accessor>(
          "udp.connect.target_port");

  std::string target_port;
  if (dynamic_port != nullptr && dynamic_port->value() > 0 && dynamic_port->value() <= 65535) {
    target_port = std::to_string(dynamic_port->value());
  } else {
    target_port = std::to_string(tunnel_config_.defaultTargetPort());
  }

  // TODO(ohadvano): support configurable URI template.
  return absl::StrCat("/.well-known/masque/udp/", target_host, "/", target_port, "/");
}

HttpUpstreamImpl::~HttpUpstreamImpl() { resetEncoder(Network::ConnectionEvent::LocalClose); }

void HttpUpstreamImpl::resetEncoder(Network::ConnectionEvent event, bool by_downstream) {
  if (!request_encoder_) {
    return;
  }

  request_encoder_->getStream().removeCallbacks(*this);
  if (by_downstream) {
    request_encoder_->getStream().resetStream(Http::StreamResetReason::LocalReset);
  }

  request_encoder_ = nullptr;

  if (!by_downstream) {
    // If we did not receive a valid CONNECT response yet we treat this as a pool
    // failure, otherwise we forward the event downstream.
    if (tunnel_creation_callbacks_.has_value()) {
      tunnel_creation_callbacks_.value().get().onStreamFailure();
      return;
    }

    upstream_callbacks_.onUpstreamEvent(event);
  }
}

TunnelingConnectionPoolImpl::TunnelingConnectionPoolImpl(
    Upstream::ThreadLocalCluster& thread_local_cluster, Upstream::LoadBalancerContext* context,
    const UdpTunnelingConfig& tunnel_config, UpstreamTunnelCallbacks& upstream_callbacks,
    StreamInfo::StreamInfo& downstream_info)
    : upstream_callbacks_(upstream_callbacks), tunnel_config_(tunnel_config),
      downstream_info_(downstream_info) {
  // TODO(ohadvano): support upstream HTTP/3.
  absl::optional<Http::Protocol> protocol = Http::Protocol::Http2;
  conn_pool_data_ =
      thread_local_cluster.httpConnPool(Upstream::ResourcePriority::Default, protocol, context);
}

void TunnelingConnectionPoolImpl::newStream(HttpStreamCallbacks& callbacks) {
  callbacks_ = &callbacks;
  upstream_ =
      std::make_unique<HttpUpstreamImpl>(upstream_callbacks_, tunnel_config_, downstream_info_);
  Tcp::ConnectionPool::Cancellable* handle =
      conn_pool_data_.value().newStream(upstream_->responseDecoder(), *this,
                                        {/*can_send_early_data_=*/false,
                                         /*can_use_http3_=*/false});

  if (handle != nullptr) {
    upstream_handle_ = handle;
  }
}

void TunnelingConnectionPoolImpl::onPoolFailure(Http::ConnectionPool::PoolFailureReason reason,
                                                absl::string_view failure_reason,
                                                Upstream::HostDescriptionConstSharedPtr host) {
  upstream_handle_ = nullptr;
  // Writing to downstream_info_ before calling onStreamFailure, as the session could be potentially
  // removed by onStreamFailure, which will cause downstream_info_ to be freed.
  downstream_info_.upstreamInfo()->setUpstreamHost(host);
  downstream_info_.upstreamInfo()->setUpstreamTransportFailureReason(failure_reason);
  callbacks_->onStreamFailure(reason, failure_reason, host);
}

void TunnelingConnectionPoolImpl::onPoolReady(Http::RequestEncoder& request_encoder,
                                              Upstream::HostDescriptionConstSharedPtr upstream_host,
                                              StreamInfo::StreamInfo& upstream_info,
                                              absl::optional<Http::Protocol>) {
  auto upstream_connection_id = upstream_info.downstreamAddressProvider().connectionID().value();
  ENVOY_LOG(debug, "Upstream connection [C{}] ready, creating tunnel stream",
            upstream_connection_id);

  upstream_handle_ = nullptr;
  upstream_host_ = upstream_host;
  upstream_info_ = &upstream_info;
  ssl_info_ = upstream_info.downstreamAddressProvider().sslConnection();

  bool is_ssl = upstream_host->transportSocketFactory().implementsSecureTransport();
  upstream_->setRequestEncoder(request_encoder, is_ssl);
  upstream_->setTunnelCreationCallbacks(*this);
  downstream_info_.upstreamInfo()->setUpstreamHost(upstream_host);
  downstream_info_.setUpstreamBytesMeter(request_encoder.getStream().bytesMeter());
  downstream_info_.upstreamInfo()->setUpstreamConnectionId(upstream_connection_id);
  callbacks_->resetIdleTimer();
}

TunnelingConnectionPoolPtr TunnelingConnectionPoolFactory::createConnPool(
    Upstream::ThreadLocalCluster& thread_local_cluster, Upstream::LoadBalancerContext* context,
    const UdpTunnelingConfig& tunnel_config, UpstreamTunnelCallbacks& upstream_callbacks,
    StreamInfo::StreamInfo& downstream_info) const {
  auto pool = std::make_unique<TunnelingConnectionPoolImpl>(
      thread_local_cluster, context, tunnel_config, upstream_callbacks, downstream_info);
  return (pool->valid() ? std::move(pool) : nullptr);
}

UdpProxyFilter::TunnelingActiveSession::TunnelingActiveSession(
    ClusterInfo& cluster, Network::UdpRecvData::LocalPeerAddresses&& addresses)
    : ActiveSession(cluster, std::move(addresses), nullptr) {}

bool UdpProxyFilter::TunnelingActiveSession::createUpstream() {
  if (conn_pool_factory_) {
    // A session filter may call on continueFilterChain(), after already creating the upstream,
    // so we first check that the factory was not created already.
    return true;
  }

  conn_pool_factory_ = std::make_unique<TunnelingConnectionPoolFactory>();
  load_balancer_context_ = std::make_unique<UdpLoadBalancerContext>(
      cluster_.filter_.config_->hashPolicy(), addresses_.peer_, &udp_session_info_);

  return establishUpstreamConnection();
}

bool UdpProxyFilter::TunnelingActiveSession::establishUpstreamConnection() {
  if (!createConnectionPool()) {
    ENVOY_LOG(debug, "failed to create upstream connection pool");
    cluster_.cluster_stats_.sess_tunnel_failure_.inc();
    return false;
  }

  return true;
}

bool UdpProxyFilter::TunnelingActiveSession::createConnectionPool() {
  ASSERT(conn_pool_factory_);

  // Check this here because the TCP conn pool will queue our request waiting for a connection that
  // will never be released.
  if (!cluster_.cluster_.info()
           ->resourceManager(Upstream::ResourcePriority::Default)
           .connections()
           .canCreate()) {
    udp_session_info_.setResponseFlag(StreamInfo::CoreResponseFlag::UpstreamOverflow);
    cluster_.cluster_.info()->trafficStats()->upstream_cx_overflow_.inc();
    return false;
  }

  if (connect_attempts_ >= cluster_.filter_.config_->tunnelingConfig()->maxConnectAttempts()) {
    udp_session_info_.setResponseFlag(StreamInfo::CoreResponseFlag::UpstreamRetryLimitExceeded);
    cluster_.cluster_.info()->trafficStats()->upstream_cx_connect_attempts_exceeded_.inc();
    return false;
  } else if (connect_attempts_ >= 1) {
    cluster_.cluster_.info()->trafficStats()->upstream_rq_retry_.inc();
  }

  conn_pool_ = conn_pool_factory_->createConnPool(cluster_.cluster_, load_balancer_context_.get(),
                                                  *cluster_.filter_.config_->tunnelingConfig(),
                                                  *this, udp_session_info_);

  if (conn_pool_) {
    connecting_ = true;
    connect_attempts_++;
    udp_session_info_.setAttemptCount(connect_attempts_);
    conn_pool_->newStream(*this);
    return true;
  }

  udp_session_info_.setResponseFlag(StreamInfo::CoreResponseFlag::NoHealthyUpstream);
  return false;
}

void UdpProxyFilter::TunnelingActiveSession::onStreamFailure(
    ConnectionPool::PoolFailureReason reason, absl::string_view failure_reason,
    Upstream::HostDescriptionConstSharedPtr) {
  ENVOY_LOG(debug, "Failed to create upstream stream: {}", failure_reason);

  conn_pool_.reset();
  upstream_.reset();

  switch (reason) {
  case ConnectionPool::PoolFailureReason::Overflow:
  case ConnectionPool::PoolFailureReason::LocalConnectionFailure:
    onUpstreamEvent(Network::ConnectionEvent::LocalClose);
    break;
  case ConnectionPool::PoolFailureReason::Timeout:
    udp_session_info_.setResponseFlag(StreamInfo::CoreResponseFlag::UpstreamConnectionFailure);
    onUpstreamEvent(Network::ConnectionEvent::RemoteClose);
    break;
  case ConnectionPool::PoolFailureReason::RemoteConnectionFailure:
    if (connecting_) {
      udp_session_info_.setResponseFlag(StreamInfo::CoreResponseFlag::UpstreamConnectionFailure);
    }
    onUpstreamEvent(Network::ConnectionEvent::RemoteClose);
    break;
  }
}

void UdpProxyFilter::TunnelingActiveSession::onStreamReady(StreamInfo::StreamInfo* upstream_info,
                                                           std::unique_ptr<HttpUpstream>&& upstream,
                                                           Upstream::HostDescriptionConstSharedPtr&,
                                                           const Network::ConnectionInfoProvider&,
                                                           Ssl::ConnectionInfoConstSharedPtr) {
  // TODO(ohadvano): save the host description to host_ field. This requires refactoring because
  // currently host_ is of type HostConstSharedPtr and not HostDescriptionConstSharedPtr.
  ENVOY_LOG(debug, "Upstream connection [C{}] attached to session ID [S{}]",
            upstream_info->downstreamAddressProvider().connectionID().value(), sessionId());

  upstream_ = std::move(upstream);
  conn_pool_.reset();
  connecting_ = false;
  can_send_upstream_ = true;
  cluster_.cluster_stats_.sess_tunnel_success_.inc();

  if (cluster_.filter_.config_->flushAccessLogOnTunnelConnected()) {
    fillSessionStreamInfo();
    const Formatter::HttpFormatterContext log_context{
        nullptr, nullptr, nullptr, {}, AccessLog::AccessLogType::UdpTunnelUpstreamConnected};
    for (const auto& access_log : cluster_.filter_.config_->sessionAccessLogs()) {
      access_log->log(log_context, udp_session_info_);
    }
  }

  flushBuffer();
}

void UdpProxyFilter::TunnelingActiveSession::onUpstreamEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::Connected ||
      event == Network::ConnectionEvent::ConnectedZeroRtt) {
    return;
  }

  bool connecting = connecting_;
  connecting_ = false;

  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    upstream_.reset();

    if (!connecting || !establishUpstreamConnection()) {
      cluster_.removeSession(this);
    }
  }
}

void UdpProxyFilter::TunnelingActiveSession::onAboveWriteBufferHighWatermark() {
  can_send_upstream_ = false;
}

void UdpProxyFilter::TunnelingActiveSession::onBelowWriteBufferLowWatermark() {
  can_send_upstream_ = true;
  flushBuffer();
}

void UdpProxyFilter::TunnelingActiveSession::flushBuffer() {
  while (!datagrams_buffer_.empty()) {
    BufferedDatagramPtr buffered_datagram = std::move(datagrams_buffer_.front());
    datagrams_buffer_.pop();
    buffered_bytes_ -= buffered_datagram->buffer_->length();
    upstream_->encodeData(*buffered_datagram->buffer_);
  }
}

void UdpProxyFilter::TunnelingActiveSession::maybeBufferDatagram(Network::UdpRecvData& data) {
  if (!cluster_.filter_.config_->tunnelingConfig()->bufferEnabled()) {
    return;
  }

  if (datagrams_buffer_.size() ==
          cluster_.filter_.config_->tunnelingConfig()->maxBufferedDatagrams() ||
      buffered_bytes_ + data.buffer_->length() >
          cluster_.filter_.config_->tunnelingConfig()->maxBufferedBytes()) {
    cluster_.cluster_stats_.sess_tunnel_buffer_overflow_.inc();
    return;
  }

  auto buffered_datagram = std::make_unique<Network::UdpRecvData>();
  buffered_datagram->addresses_ = {std::move(data.addresses_.local_),
                                   std::move(data.addresses_.peer_)};
  buffered_datagram->buffer_ = std::move(data.buffer_);
  buffered_datagram->receive_time_ = data.receive_time_;
  buffered_bytes_ += buffered_datagram->buffer_->length();
  datagrams_buffer_.push(std::move(buffered_datagram));
}

void UdpProxyFilter::TunnelingActiveSession::writeUpstream(Network::UdpRecvData& data) {
  if (!upstream_ || !can_send_upstream_) {
    maybeBufferDatagram(data);
    return;
  }

  if (upstream_) {
    upstream_->encodeData(*data.buffer_);
  }
}

void UdpProxyFilter::TunnelingActiveSession::onUpstreamData(Buffer::Instance& data, bool) {
  const uint64_t rx_buffer_length = data.length();
  ENVOY_LOG(trace, "received {} byte datagram from upstream: downstream={} local={} upstream={}",
            rx_buffer_length, addresses_.peer_->asStringView(), addresses_.local_->asStringView(),
            host_ != nullptr ? host_->address()->asStringView() : "unknown");

  cluster_.cluster_stats_.sess_rx_datagrams_.inc();
  cluster_.cluster_.info()->trafficStats()->upstream_cx_rx_bytes_total_.add(rx_buffer_length);
  resetIdleTimer();

  Network::UdpRecvData recv_data{{addresses_.local_, addresses_.peer_},
                                 std::make_unique<Buffer::OwnedImpl>(data),
                                 cluster_.filter_.config_->timeSource().monotonicTime(),
                                 0,
                                 {}};
  processUpstreamDatagram(recv_data);
}

void UdpProxyFilter::TunnelingActiveSession::onIdleTimer() {
  ENVOY_LOG(debug, "session idle timeout: downstream={} local={}", addresses_.peer_->asStringView(),
            addresses_.local_->asStringView());
  udp_session_info_.setResponseFlag(StreamInfo::CoreResponseFlag::StreamIdleTimeout);
  cluster_.filter_.config_->stats().idle_timeout_.inc();

  if (upstream_) {
    upstream_->onDownstreamEvent(Network::ConnectionEvent::LocalClose);
  } else if (conn_pool_) {
    conn_pool_->onDownstreamEvent(Network::ConnectionEvent::LocalClose);
  }

  cluster_.removeSession(this);
}

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
