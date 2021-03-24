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

void ActiveMySQLClient::Auther::onServerGreeting(ServerGreeting& greet) {
  auto auth_method = AuthHelper::authMethod(greet.getBaseServerCap(), greet.getExtServerCap(),
                                            greet.getAuthPluginName());
  if (auth_method != AuthMethod::OldPassword && auth_method != AuthMethod::NativePassword) {
    // If server's auth method is not supported, use mysql_native_password as response, wait for
    // auth switch.
    auth_method = AuthMethod::NativePassword;
  }
  seed_ = greet.getAuthPluginData();
  auto handshake_resp = MessageHelper::encodeClientLogin(auth_method, parent_.username_,
                                                         parent_.password_, parent_.db_, seed_);
  auto& session = decoder_->getSession();
  auto seq = session.getExpectedSeq();
  parent_.makeRequest(handshake_resp, seq);
  // skip MySQLSession::State::ChallengeReq state
  session.setState(MySQLSession::State::ChallengeResp41);
  session.setExpectedSeq(seq + 1);
}

void ActiveMySQLClient::Auther::onClientLogin(ClientLogin&) {
  parent_.onFailure(MySQLPoolFailureReason::ParseFailure);
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
        parent_.onFailure(MySQLPoolFailureReason::AuthFailure);
        return;
      }
    }

    auto auth_switch_resp = MessageHelper::encodeClientLogin(auth_method, parent_.username_,
                                                             parent_.password_, parent_.db_, seed_);
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
    parent_.onFailure(MySQLPoolFailureReason::AuthFailure);
    break;
  case MYSQL_RESP_MORE:
  default:
    ENVOY_LOG(info, "unhandled message resp code {}", client_login_resp.getRespCode());
    parent_.onFailure(MySQLPoolFailureReason::ParseFailure);
    break;
  }
}

void ActiveMySQLClient::Auther::onClientSwitchResponse(ClientSwitchResponse&) {
  parent_.onFailure(MySQLPoolFailureReason::ParseFailure);
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
    parent_.onFailure(MySQLPoolFailureReason::AuthFailure);
    break;
  case MYSQL_RESP_AUTH_SWITCH:
  default:
    ENVOY_LOG(trace, "unhandled message resp code {}", client_login_resp.getRespCode());
    parent_.onFailure(MySQLPoolFailureReason::ParseFailure);
    break;
  }
}

void ActiveMySQLClient::Auther::onCommand(Command&) {
  parent_.onFailure(MySQLPoolFailureReason::ParseFailure);
  ENVOY_LOG(info, "impossible callback called on client side");
}

void ActiveMySQLClient::Auther::onCommandResponse(CommandResponse&) {
  parent_.onFailure(MySQLPoolFailureReason::ParseFailure);
  ENVOY_LOG(info, "impossible callback called on client side, idle client should not receive the "
                  "command response of server");
}

ActiveMySQLClient::ActiveMySQLClient(Envoy::ConnectionPool::ConnPoolImplBase& parent,
                                     const Upstream::HostConstSharedPtr& host,
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

} // namespace ConnPool
} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
