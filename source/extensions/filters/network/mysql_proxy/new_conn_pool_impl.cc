#include "extensions/filters/network/mysql_proxy/new_conn_pool_impl.h"

#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/upstream/upstream.h"

#include "common/stats/timespan_impl.h"
#include "common/upstream/upstream_impl.h"
#include "extensions/filters/network/mysql_proxy/message_helper.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {
namespace ConnPool {

std::string ActiveMySQLClient::Auther::username() const {
  return parent_.parent_.parent_.username_;
}

std::string ActiveMySQLClient::Auther::password() const {
  return parent_.parent_.parent_.password_;
}

std::string ActiveMySQLClient::Auther::database() const {
  return parent_.parent_.parent_.database_;
}

void ActiveMySQLClient::Auther::onServerGreeting(ServerGreeting& greet) {
  auto auth_method = AuthHelper::authMethod(greet.getBaseServerCap(), greet.getExtServerCap(),
                                            greet.getAuthPluginName());
  if (auth_method != AuthMethod::OldPassword && auth_method != AuthMethod::NativePassword) {
    // If server's auth method is not supported, use mysql_native_password as response, wait for
    // auth switch.
    auth_method = AuthMethod::NativePassword;
  }
  seed_ = greet.getAuthPluginData();
  auto handshake_resp =
      MessageHelper::encodeClientLogin(auth_method, username(), password(), database(), seed_);
  auto& session = decoder_->getSession();
  auto seq = session.getExpectedSeq();
  parent_.makeRequest(handshake_resp, seq);
  // skip MySQLSession::State::ChallengeReq state
  session.setState(MySQLSession::State::ChallengeResp41);
  session.setExpectedSeq(seq + 1);
}

void ActiveMySQLClient::Auther::onClientLogin(ClientLogin&) {
  onFailure(MySQLPoolFailureReason::ParseFailure);
  ENVOY_LOG(info, "impossible callback called on client side");
}

void ActiveMySQLClient::Auther::onClientLoginResponse(ClientLoginResponse& client_login_resp) {
  switch (client_login_resp.getRespCode()) {
  case MYSQL_RESP_AUTH_SWITCH: {
    auto auth_method = AuthMethod::OldPassword;
    auto& auth_switch = dynamic_cast<AuthSwitchMessage&>(client_login_resp);
    if (!auth_switch.isOldAuthSwitch()) {
      auth_method = AuthMethod::NativePassword;
      seed_ = auth_switch.getAuthPluginData();
      if (auth_switch.getAuthPluginName() != "mysql_native_password") {
        ENVOY_LOG(info, "auth plugin {} is not supported", auth_switch.getAuthPluginName());
        onFailure(MySQLPoolFailureReason::AuthFailure);
        return;
      }
    }

    auto auth_switch_resp =
        MessageHelper::encodeClientLogin(auth_method, username(), password(), database(), seed_);
    auto& session = decoder_->getSession();
    auto seq = session.getExpectedSeq();
    parent_.makeRequest(auth_switch_resp, seq);
    session.setExpectedSeq(seq + 1);
    session.setState(MySQLSession::State::AuthSwitchMore);
    break;
  }
  case MYSQL_RESP_OK:
    parent_.onAuthPassed();
    break;
  case MYSQL_RESP_ERR:
    ENVOY_LOG(info, "mysql proxy failed of auth, info {}",
              dynamic_cast<ErrMessage&>(client_login_resp).getErrorMessage());
    onFailure(MySQLPoolFailureReason::AuthFailure);
    break;
  case MYSQL_RESP_MORE:
  default:
    ENVOY_LOG(info, "unhandled message resp code {}", client_login_resp.getRespCode());
    onFailure(MySQLPoolFailureReason::ParseFailure);
    break;
  }
}

void ActiveMySQLClient::Auther::onClientSwitchResponse(ClientSwitchResponse&) {
  onFailure(MySQLPoolFailureReason::ParseFailure);
  ENVOY_LOG(info, "impossible callback called on client side");
}

void ActiveMySQLClient::Auther::onMoreClientLoginResponse(ClientLoginResponse& client_login_resp) {
  switch (client_login_resp.getRespCode()) {
  case MYSQL_RESP_MORE:
  case MYSQL_RESP_OK:
    parent_.onAuthPassed();
    break;
  case MYSQL_RESP_ERR:
    ENVOY_LOG(info, "mysql proxy failed of auth, info {} ",
              dynamic_cast<ErrMessage&>(client_login_resp).getErrorMessage());
    onFailure(MySQLPoolFailureReason::AuthFailure);
    break;
  case MYSQL_RESP_AUTH_SWITCH:
  default:
    ENVOY_LOG(trace, "unhandled message resp code {}", client_login_resp.getRespCode());
    onFailure(MySQLPoolFailureReason::ParseFailure);
    break;
  }
}

void ActiveMySQLClient::Auther::onCommand(Command&) {
  onFailure(MySQLPoolFailureReason::ParseFailure);
  ENVOY_LOG(info, "impossible callback called on client side");
}

void ActiveMySQLClient::Auther::onCommandResponse(CommandResponse&) {
  onFailure(MySQLPoolFailureReason::ParseFailure);
  ENVOY_LOG(info, "impossible callback called on client side, idle client should not receive the "
                  "command response of server");
}

ActiveMySQLClient::ActiveMySQLClient(ConnPoolImpl& parent, const Upstream::HostConstSharedPtr& host,
                                     uint64_t concurrent_stream_limit)
    : Envoy::ConnectionPool::ActiveClient(parent, host->cluster().maxRequestsPerConnection(),
                                          concurrent_stream_limit),
      parent_(parent) {
  Upstream::Host::CreateConnectionData data = host->createConnection(
      parent_.dispatcher(), parent_.socketOptions(), parent_.transportSocketOptions());
  real_host_description_ = data.host_description_;
  connection_ = std::move(data.connection_);
  connection_->addConnectionCallbacks(*this);
  read_filter_handle_ = std::make_shared<Auther>(*this);
  connection_->addReadFilter(read_filter_handle_);
  connection_->setConnectionStats({host->cluster().stats().upstream_cx_rx_bytes_total_,
                                   host->cluster().stats().upstream_cx_rx_bytes_buffered_,
                                   host->cluster().stats().upstream_cx_tx_bytes_total_,
                                   host->cluster().stats().upstream_cx_tx_bytes_buffered_,
                                   &host->cluster().stats().bind_errors_, nullptr});
  connection_->noDelay(true);
  connection_->connect();
}

ActiveMySQLClient::~ActiveMySQLClient() {
  // Handle the case where deferred delete results in the ActiveClient being destroyed before
  // TcpConnectionData. Make sure the TcpConnectionData will not refer to this ActiveMySQLClient
  // and handle clean up normally done in clearCallbacks()
  if (tcp_connection_data_) {
    ASSERT(state_ == ActiveClient::State::CLOSED);
    tcp_connection_data_->release();
    parent_.onStreamClosed(*this, true);
    parent_.checkForDrained();
  }
}

void ActiveMySQLClient::clearCallbacks() {
  if (state_ == Envoy::ConnectionPool::ActiveClient::State::BUSY && parent_.hasPendingStreams()) {
    auto* pool = &parent_;
    pool->scheduleOnUpstreamReady();
  }
  callbacks_ = nullptr;
  tcp_connection_data_ = nullptr;
  parent_.onStreamClosed(*this, true);
  parent_.checkForDrained();
}

void ActiveMySQLClient::onAuthPassed() {
  connection_->readDisable(true);
  connection_->removeReadFilter(read_filter_handle_);

  parent_.transitionActiveClientState(*this, ActiveClient::State::READY);
  parent_.scheduleOnUpstreamReady();
}

void ActiveMySQLClient::Auther::onFailure(MySQLPoolFailureReason reason) {
  ASSERT(reason != MySQLPoolFailureReason::RemoteConnectionFailure ||
         reason != MySQLPoolFailureReason::LocalConnectionFailure ||
         reason != MySQLPoolFailureReason::Overflow);
  parent_.parent_.onMySQLFailure(reason);
}

void ActiveMySQLClient::makeRequest(MySQLCodec& codec, uint8_t seq) {
  Buffer::OwnedImpl buffer;
  codec.encode(buffer);
  BufferHelper::encodeHdr(buffer, seq);
  connection_->write(buffer, false);
}

void ActiveMySQLClient::onEvent(Network::ConnectionEvent event) {
  // when connection complete, we need wait for authenticate phase passed till poolReady
  if (event == Network::ConnectionEvent::Connected) {
    conn_connect_ms_->complete();
    conn_connect_ms_.reset();
    ASSERT(state_ == ActiveClient::State::CONNECTING);
    parent_.checkForDrained();
    return;
  }
  Envoy::ConnectionPool::ActiveClient::onEvent(event);
  if (callbacks_) {
    if (tcp_connection_data_) {
      Envoy::Upstream::reportUpstreamCxDestroyActiveRequest(parent_.host(), event);
    }
    callbacks_->onEvent(event);
    callbacks_ = nullptr;
  }
}

void ConnPoolImpl::onMySQLFailure(MySQLPoolFailureReason reason) {
  ASSERT(reason == MySQLPoolFailureReason::ParseFailure ||
         reason == MySQLPoolFailureReason::AuthFailure);
  // It's always nothing helpful to retry auth when auth or parse failure occurs. It always means
  // the username or password or db or protcol error.
  state_.decrPendingStreams(pending_streams_.size());
  pending_streams_to_purge_ = std::move(pending_streams_);
  while (!pending_streams_to_purge_.empty()) {
    auto stream = pending_streams_to_purge_.front()->removeFromList(pending_streams_to_purge_);
    host_->cluster().stats().upstream_rq_pending_failure_eject_.inc();
    auto* callbacks = typedContext<MySQLAttachContext>(stream->context()).callbacks_;
    callbacks->onPoolFailure(static_cast<MySQLPoolFailureReason>(reason), host_);
  }
}

ConnectionPoolManagerImpl::ConnectionPoolManagerImpl(Upstream::ClusterManager* cm,
                                                     ThreadLocal::SlotAllocator& tls)
    : cm_(cm), tls_(tls.allocateSlot()) {}

ConnectionPool::Cancellable*
ConnectionPoolManagerImpl::newConnection(ClientPoolCallBack& callbacks) {
  return tls_->getTyped<ThreadLocalPool>().newConnection(callbacks);
}

void ConnectionPoolManagerImpl::init(
    const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::Route& route,
    const std::string& username, const std::string& password) {
  std::weak_ptr<ConnectionPoolManagerImpl> this_weak_ptr = shared_from_this();
  auto cm = cm_;
  tls_->set([this_weak_ptr, route, username, password,
             cm](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    if (auto this_shared_ptr = this_weak_ptr.lock()) {
      auto pool = std::make_shared<ThreadLocalPool>(this_shared_ptr, dispatcher, cm, route,
                                                    username, password);
      return pool;
    }
    return nullptr;
  });
}

ThreadLocalPool::ThreadLocalPool(
    std::weak_ptr<ConnectionPoolManagerImpl> parent, Event::Dispatcher& dispatcher,
    Upstream::ClusterManager* cm,
    const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::Route& route,
    const std::string& username, const std::string& password)
    : parent_(parent), dispatcher_(dispatcher), username_(username), password_(password),
      cluster_name_(route.cluster()), database_(route.database()) {
  auto _parent = parent.lock();
  ASSERT(_parent != nullptr);
  cluster_update_handle_ = _parent->cm_->addThreadLocalClusterUpdateCallbacks(*this);
  Upstream::ThreadLocalCluster* cluster = _parent->cm_->getThreadLocalCluster(cluster_name_);
  if (cluster != nullptr) {
    auth_username_ = ProtocolOptionsConfigImpl::authUsername(cluster->info(), parent->api_);
    auth_password_ = ProtocolOptionsConfigImpl::authPassword(cluster->info(), parent->api_);
    onClusterAddOrUpdateNonVirtual(*cluster);
  }
}

void ThreadLocalPool::onClusterAddOrUpdateNonVirtual(Upstream::ThreadLocalCluster& cluster) {
  if (cluster.info()->name() != cluster_name_) {
    return;
  }
  // Ensure the filter is not deleted in the main thread during this method.
  auto shared_parent = parent_.lock();
  if (!shared_parent) {
    return;
  }

  if (cluster_ != nullptr) {
    // Treat an update as a removal followed by an add.
    ThreadLocalPool::onClusterRemoval(cluster_name_);
  }

  ASSERT(cluster_ == nullptr);
  cluster_ = &cluster;
  ASSERT(host_set_member_update_cb_handle_ == nullptr);
  host_set_member_update_cb_handle_ = cluster_->prioritySet().addMemberUpdateCb(
      [this](const std::vector<Upstream::HostSharedPtr>& hosts_added,
             const std::vector<Upstream::HostSharedPtr>& hosts_removed) -> void {
        onHostsAdded(hosts_added);
        onHostsRemoved(hosts_removed);
      });

  ASSERT(host_address_map_.empty());
  for (const auto& i : cluster_->prioritySet().hostSetsPerPriority()) {
    for (auto& host : i->hosts()) {
      host_address_map_[host->address()->asString()] = host;
    }
  }
}

void ThreadLocalPool::onHostsAdded(const std::vector<Upstream::HostSharedPtr>& hosts_added) {
  for (const auto& host : hosts_added) {
    std::string host_address = host->address()->asString();
    // Insert new host into address map, possibly overwriting a previous host's entry.
    host_address_map_[host_address] = host;
  }
}

void ThreadLocalPool::onHostsRemoved(const std::vector<Upstream::HostSharedPtr>& hosts_removed) {
  for (const auto& host : hosts_removed) {
    auto it = pools_.find(host);
    if (it != pools_.end()) {
      it->second->drainConnections();
      pools_to_drain_.push_back(std::move(it->second));
      pools_.erase(it);
    }
    auto it2 = host_address_map_.find(host->address()->asString());
    if ((it2 != host_address_map_.end()) && (it2->second == host)) {
      host_address_map_.erase(it2);
    }
  }
}

void ThreadLocalPool::onClusterRemoval(const std::string& cluster_name) {
  if (cluster_name != cluster_name_) {
    return;
  }

  // Treat cluster removal as a removal of all hosts. Close all connections and fail all pending
  // requests.
  host_set_member_update_cb_handle_ = nullptr;
  while (!pools_.empty()) {
    pools_.begin()->second->closeConnections();
    pools_.erase(pools_.begin());
  }
  while (!pools_to_drain_.empty()) {
    pools_to_drain_.front()->closeConnections();
    pools_to_drain_.pop_front();
  }
  cluster_ = nullptr;
  host_address_map_.clear();
}

ConnectionPool::Cancellable* ThreadLocalPool::newConnection(ClientPoolCallBack& callbacks) {
  if (cluster_ == nullptr) {
    ASSERT(pools_.empty());
    ASSERT(pools_to_drain_.empty());
    ASSERT(host_set_member_update_cb_handle_ == nullptr);
    callbacks.onPoolFailure(MySQLPoolFailureReason::RemoteConnectionFailure, nullptr);
    return nullptr;
  }

  // TODO(qinggniq) add custom load balancer context
  Upstream::HostConstSharedPtr host = cluster_->loadBalancer().chooseHost(nullptr);
  if (!host) {
    ENVOY_LOG(debug, "no host in cluster {}", cluster_name_);
    callbacks.onPoolFailure(MySQLPoolFailureReason::RemoteConnectionFailure, nullptr);
    return nullptr;
  }

  auto parent = parent_.lock();
  // newConnection is only possible called from connection manager
  ASSERT(parent != nullptr);

  if (pools_.find(host) == pools_.end()) {
    pools_[host] =
        std::make_unique<ConnPoolImpl>(dispatcher_, host, Upstream::ResourcePriority::Default,
                                       nullptr, nullptr, parent->cluster_manager_state_, *this);
  }

  return pools_[host]->newConnection(callbacks);
}

} // namespace ConnPool
} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
