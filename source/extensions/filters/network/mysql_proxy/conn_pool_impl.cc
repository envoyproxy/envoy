#include "extensions/filters/network/mysql_proxy/conn_pool_impl.h"

#include <memory>

#include "envoy/api/api.h"
#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"
#include "envoy/network/connection.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "common/common/linked_object.h"
#include "common/common/logger.h"

#include "extensions/filters/network/mysql_proxy/conn_pool.h"
#include "extensions/filters/network/mysql_proxy/message_helper.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_command.h"
#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {
namespace ConnectionPool {

InstanceImpl::InstanceImpl(ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager* cm)
    : cm_(cm), tls_(tls.allocateSlot()) {}

void InstanceImpl::init(Upstream::ClusterManager* cm, DecoderFactory& decoder_factory,
                        const ConnectionPoolSettings& config, const std::string& auth_username,
                        const std::string& auth_password) {
  std::weak_ptr<InstanceImpl> this_weak_ptr = shared_from_this();
  tls_->set([this_weak_ptr, cm, config, &decoder_factory, auth_username, auth_password](
                Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    if (auto this_shared_ptr = this_weak_ptr.lock()) {
      auto pool = std::make_shared<ThreadLocalClientPool>(
          this_shared_ptr, cm, dispatcher, config, decoder_factory, auth_username, auth_password);
      pool->initStartClients();
      return pool;
    }
    return nullptr;
  });
}

Cancellable* InstanceImpl::newMySQLClient(ClientPoolCallBack& callbacks) {
  return tls_->getTyped<ThreadLocalClientPool>().newMySQLClient(callbacks);
}

InstanceImpl::ThreadLocalClientPool::ThreadLocalClientPool(std::shared_ptr<InstanceImpl> parent,
                                                           Upstream::ClusterManager* cm,
                                                           Event::Dispatcher& dispatcher,
                                                           const ConnectionPoolSettings& config,
                                                           DecoderFactory& decoder_factory,
                                                           const std::string& auth_username,
                                                           const std::string& auth_password)
    : parent_(parent), dispatcher_(dispatcher), decoder_factory_(decoder_factory), config_(config),
      auth_username_(auth_username), auth_password_(auth_password) {
  auto cluster = cm->getThreadLocalCluster(config_.cluster);

  conn_pool_ = cluster->tcpConnPool(Upstream::ResourcePriority::Default, nullptr);
}

Cancellable* InstanceImpl::ThreadLocalClientPool::newMySQLClient(ClientPoolCallBack& call_backs) {
  if (!active_clients_.empty()) {
    ThreadLocalActiveClient& client = *active_clients_.back();
    client.state_ = ClientState::Busy;
    client.moveBetweenLists(active_clients_, busy_clients_);
    call_backs.onClientReady(std::make_unique<ClientDataImpl>(client.client_wrapper_));
    return nullptr;
  }
  auto pending_request = std::make_unique<PendingRequest>(*this, call_backs);
  LinkedList::moveIntoList(std::move(pending_request), pending_requests_);
  if (active_clients_.size() + pending_clients_.size() + busy_clients_.size() >=
      config_.max_connections) {
    return pending_requests_.front().get();
  }
  createNewClient();
  return pending_requests_.front().get();
}

InstanceImpl::ThreadLocalClientPool::~ThreadLocalClientPool() {
  while (!pending_requests_.empty()) {
    pending_requests_.pop_front();
  }
  while (!pending_clients_.empty()) {
    if (pending_clients_.front()->client_wrapper_) {
      pending_clients_.front()->client_wrapper_->conn_data_->connection().close(
          Network::ConnectionCloseType::NoFlush);
    } else {
      pending_clients_.pop_front();
    }
  }
  while (!active_clients_.empty()) {
    active_clients_.front()->client_wrapper_->conn_data_->connection().close(
        Network::ConnectionCloseType::NoFlush);
  }
  while (!busy_clients_.empty()) {
    busy_clients_.front()->client_wrapper_->conn_data_->connection().close(
        Network::ConnectionCloseType::NoFlush);
  }
}

void InstanceImpl::ThreadLocalClientPool::initStartClients() {
  while (pending_clients_.size() < config_.start_connections) {
    createNewClient();
  }
}

void InstanceImpl::ThreadLocalClientPool::createNewClient() {
  auto client = std::make_unique<ThreadLocalActiveClient>(*this);
  LinkedList::moveIntoList(std::move(client), pending_clients_);
  conn_pool_->newConnection(*pending_clients_.front());
}

void InstanceImpl::ThreadLocalClientPool::removeClient(ThreadLocalActiveClientPtr&& client) {
  client->state_ = ClientState::Stop;
  dispatcher_.deferredDelete(std::move(client));
}

void InstanceImpl::ThreadLocalClientPool::processIdleClient(ThreadLocalActiveClient& client,
                                                            bool new_client) {
  ASSERT(pending_clients_.size() + active_clients_.size() + busy_clients_.size() <=
         config_.max_connections);
  if (client.client_wrapper_->decoder_ != nullptr) {
    client.client_wrapper_->decoder_.reset();
  }
  ThreadLocalActiveClientPtr removed;
  if (pending_requests_.empty()) {
    client.state_ = ClientState::Ready;
    if (new_client) {
      client.moveBetweenLists(pending_clients_, active_clients_);
    } else {
      client.moveBetweenLists(busy_clients_, active_clients_);
    }
    if (active_clients_.size() > config_.max_idle_connections) {
      client.client_wrapper_->conn_data_->connection().close(Network::ConnectionCloseType::NoFlush);
    }
    return;
  }
  if (new_client) {
    client.moveBetweenLists(pending_clients_, busy_clients_);
  }
  client.state_ = ClientState::Busy;
  pending_requests_.back()->callbacks_.onClientReady(
      std::make_unique<ClientDataImpl>(client.client_wrapper_));
  pending_requests_.pop_back();
}

InstanceImpl::PendingRequest::PendingRequest(ThreadLocalClientPool& pool,
                                             ClientPoolCallBack& callback)
    : parent_(pool), callbacks_(callback) {}

void InstanceImpl::PendingRequest::cancel() { removeFromList(parent_.pending_requests_); }

InstanceImpl::ClientWrapper::ClientWrapper(InstanceImpl::ThreadLocalActiveClient& parent,
                                           Tcp::ConnectionPool::ConnectionDataPtr&& conn_data)
    : parent_(parent), decoder_(parent_.parent_.decoder_factory_.create(parent_)),
      conn_data_(std::move(conn_data)) {}

InstanceImpl::ClientDataImpl::ClientDataImpl(ClientWrapperSharedPtr wrapper)
    : client_wrapper_(wrapper) {}

void InstanceImpl::ClientDataImpl::resetClient(DecoderPtr&& decoder) {
  client_wrapper_->decoder_ = std::move(decoder);
  client_wrapper_->decode_buffer_.drain(client_wrapper_->decode_buffer_.length());
}

void InstanceImpl::ClientDataImpl::sendData(Buffer::Instance& buffer) {
  client_wrapper_->connectionData().connection().write(buffer, false);
}

Decoder& InstanceImpl::ClientDataImpl::decoder() { return *client_wrapper_->decoder_; }

void InstanceImpl::ClientDataImpl::close() {
  client_wrapper_->parent_.parent_.processIdleClient(this->client_wrapper_->parent_, false);
}

InstanceImpl::ThreadLocalActiveClient::ThreadLocalActiveClient(ThreadLocalClientPool& parent)
    : parent_(parent) {}

InstanceImpl::ThreadLocalActiveClient::~ThreadLocalActiveClient() {
  ENVOY_LOG(trace, "~ThreadLocalActiveClient {}", reinterpret_cast<uint64_t>(this));
}

void InstanceImpl::ThreadLocalActiveClient::onPoolReady(
    Tcp::ConnectionPool::ConnectionDataPtr&& conn, Upstream::HostDescriptionConstSharedPtr) {
  client_wrapper_ = std::make_shared<ClientWrapper>(*this, std::move(conn));
  client_wrapper_->connectionData().addUpstreamCallbacks(*this);
}

void InstanceImpl::ThreadLocalActiveClient::onPoolFailure(
    Tcp::ConnectionPool::PoolFailureReason reason, Upstream::HostDescriptionConstSharedPtr) {
  onFailure(static_cast<MySQLPoolFailureReason>(reason));
}

void InstanceImpl::ThreadLocalActiveClient::onUpstreamData(Buffer::Instance& data, bool) {
  ASSERT(client_wrapper_ != nullptr);
  ENVOY_LOG(trace, "upstream sent data, len {}", data.length());

  try {
    if (client_wrapper_->decoder_ != nullptr) {
      client_wrapper_->decode_buffer_.move(data);
      client_wrapper_->decoder_->onData(client_wrapper_->decode_buffer_);
    } else {
      // ignore data when decoder is null
      data.drain(data.length());
    }
  } catch (EnvoyException& e) {
    ENVOY_LOG(trace, "info when decode upstream data {}", e.what());
    onFailure(MySQLPoolFailureReason::ParseFailure);
  }
}

void InstanceImpl::ThreadLocalActiveClient::onEvent(Network::ConnectionEvent event) {
  ASSERT(client_wrapper_ != nullptr);
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    ThreadLocalActiveClientPtr removed;
    switch (state_) {
    case ClientState::Uinit:
      removed = removeFromList(parent_.pending_clients_);
      break;
    case ClientState::Ready:
      removed = removeFromList(parent_.active_clients_);
      break;
    case ClientState::Busy:
      removed = removeFromList(parent_.busy_clients_);
      break;
    case ClientState::Stop:
      return;
    default:
      return;
    }
    parent_.removeClient(std::move(removed));
  }
}

void InstanceImpl::ThreadLocalActiveClient::onFailure(MySQLPoolFailureReason reason) {
  if (client_wrapper_ != nullptr) {
    client_wrapper_->connectionData().connection().close(Network::ConnectionCloseType::NoFlush);
  } else {
    auto removed = removeFromList(parent_.pending_clients_);
    parent_.removeClient(std::move(removed));
  }
  switch (reason) {
  case MySQLPoolFailureReason::RemoteConnectionFailure:
  case MySQLPoolFailureReason::LocalConnectionFailure:
  case MySQLPoolFailureReason::AuthFailure:
  case MySQLPoolFailureReason::ParseFailure:
    while (!parent_.pending_requests_.empty()) {
      parent_.pending_requests_.front()->callbacks_.onClientFailure(reason);
      parent_.pending_requests_.pop_front();
    }
    break;
  case MySQLPoolFailureReason::Overflow:
    if (!parent_.pending_requests_.empty()) {
      parent_.pending_requests_.front()->callbacks_.onClientFailure(reason);
      parent_.pending_requests_.pop_front();
    }
    break;
  default:
    if (parent_.pending_requests_.size() > parent_.pending_clients_.size() &&
        parent_.pending_requests_.size() + parent_.pending_clients_.size() +
                parent_.active_clients_.size() + parent_.busy_clients_.size() <=
            parent_.config_.max_connections) {
      parent_.createNewClient();
    }
    break;
  }
}

void InstanceImpl::ThreadLocalActiveClient::writeUpstream(MySQLCodec& codec, uint8_t expected_seq) {
  Buffer::OwnedImpl buffer;
  codec.encode(buffer);
  BufferHelper::encodeHdr(buffer, expected_seq);
  client_wrapper_->connectionData().connection().write(buffer, false);
}

void InstanceImpl::ThreadLocalActiveClient::onPassConnectionPhase() {
  parent_.processIdleClient(*this, true);
}

void InstanceImpl::ThreadLocalActiveClient::onProtocolError() {
  ENVOY_LOG(info, "proxy failed on connection phase");
  onFailure(MySQLPoolFailureReason::ParseFailure);
}

void InstanceImpl::ThreadLocalActiveClient::onServerGreeting(ServerGreeting& greet) {
  auto auth_method = AuthHelper::authMethod(greet.getBaseServerCap(), greet.getExtServerCap(),
                                            greet.getAuthPluginName());
  if (auth_method != AuthMethod::OldPassword && auth_method != AuthMethod::NativePassword) {
    // If server's auth method is not supported, use mysql_native_password as response, wait for
    // auth switch.
    auth_method = AuthMethod::NativePassword;
  }
  old_auth_data_ = greet.getAuthPluginData();
  auto handshake_resp =
      MessageHelper::encodeClientLogin(auth_method, parent_.auth_username_, parent_.auth_password_,
                                       parent_.config_.db, old_auth_data_);
  auto& session = client_wrapper_->decoder_->getSession();
  auto seq = session.getExpectedSeq();
  writeUpstream(handshake_resp, seq);
  // skip MySQLSession::State::ChallengeReq state
  session.setState(MySQLSession::State::ChallengeResp41);
  session.setExpectedSeq(seq + 1);
}

void InstanceImpl::ThreadLocalActiveClient::onClientLogin(ClientLogin&) {
  onFailure(MySQLPoolFailureReason::ParseFailure);
  ENVOY_LOG(info, "impossible callback called on client side");
}

void InstanceImpl::ThreadLocalActiveClient::onClientLoginResponse(
    ClientLoginResponse& client_login_resp) {
  switch (client_login_resp.getRespCode()) {
  case MYSQL_RESP_AUTH_SWITCH: {
    auto auth_method = AuthMethod::OldPassword;
    auto& auth_switch = dynamic_cast<AuthSwitchMessage&>(client_login_resp);
    if (!auth_switch.isOldAuthSwitch()) {
      auth_method = AuthMethod::NativePassword;
      old_auth_data_ = auth_switch.getAuthPluginData();
      if (auth_switch.getAuthPluginName() != "mysql_native_password") {
        ENVOY_LOG(info, "auth plugin {} is not supported", auth_switch.getAuthPluginName());
        onFailure(MySQLPoolFailureReason::AuthFailure);
        return;
      }
    }

    auto auth_switch_resp = MessageHelper::encodeClientLogin(auth_method, parent_.auth_username_,
                                                             parent_.auth_password_,
                                                             parent_.config_.db, old_auth_data_);
    auto& session = client_wrapper_->decoder_->getSession();
    auto seq = session.getExpectedSeq();
    writeUpstream(auth_switch_resp, seq);
    session.setExpectedSeq(seq + 1);
    session.setState(MySQLSession::State::AuthSwitchMore);
    break;
  }
  case MYSQL_RESP_OK:
    onPassConnectionPhase();
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

void InstanceImpl::ThreadLocalActiveClient::onClientSwitchResponse(ClientSwitchResponse&) {
  onFailure(MySQLPoolFailureReason::ParseFailure);
  ENVOY_LOG(info, "impossible callback called on client side");
}

void InstanceImpl::ThreadLocalActiveClient::onMoreClientLoginResponse(
    ClientLoginResponse& client_login_resp) {
  switch (client_login_resp.getRespCode()) {
  case MYSQL_RESP_MORE:
  case MYSQL_RESP_OK:
    onPassConnectionPhase();
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

void InstanceImpl::ThreadLocalActiveClient::onCommand(Command&) {
  onFailure(MySQLPoolFailureReason::ParseFailure);
  ENVOY_LOG(info, "impossible callback called on client side");
}

void InstanceImpl::ThreadLocalActiveClient::onCommandResponse(CommandResponse&) {
  onFailure(MySQLPoolFailureReason::ParseFailure);
  ENVOY_LOG(info, "impossible callback called on client side, idle client should not receive the "
                  "command response of server");
}

InstanceFactoryImpl InstanceFactoryImpl::instance_;

ClientPoolSharedPtr InstanceFactoryImpl::create(
    ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager* cm,
    const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::Route& route,
    const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::ConnectionPoolSettings&
        setting,
    DecoderFactory& decoder_factory, const std::string& auth_username,
    const std::string& auth_password) {
  auto client_pool = std::make_shared<InstanceImpl>(tls, cm);
  auto config = ConnectionPoolSettings(route, setting);
  client_pool->init(cm, decoder_factory, config, auth_username, auth_password);
  return client_pool;
}

} // namespace ConnectionPool
} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
