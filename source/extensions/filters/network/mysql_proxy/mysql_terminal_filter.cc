#include "source/extensions/filters/network/mysql_proxy/mysql_terminal_filter.h"

#include "envoy/api/api.h"
#include "envoy/buffer/buffer.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/outlier_detection.h"
#include "envoy/upstream/upstream.h"

#include "route.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/config/datasource.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_decoder.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_decoder_impl.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_filter.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_session.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_utils.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_message.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

MySQLTerminalFilter::MySQLTerminalFilter(MySQLFilterConfigSharedPtr config, RouterSharedPtr router,
                                         DecoderFactory& factory)
    : MySQLMonitorFilter(config, factory), router_(router), upstream_conn_data_(nullptr) {}

void MySQLTerminalFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  read_callbacks_->connection().addConnectionCallbacks(*this);
}

void MySQLTerminalFilter::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    ENVOY_LOG(debug, "downstream connection closed");
    if (canceler_) {
      canceler_->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
      canceler_ = nullptr;
    }
    if (upstream_conn_data_) {
      upstream_conn_data_->connection().close(Network::ConnectionCloseType::NoFlush);
    }
    read_callbacks_ = nullptr;
  }
}

void MySQLTerminalFilter::UpstreamEventHandler::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    ENVOY_LOG(debug, "upstream connection closed");
    if (parent.read_callbacks_) {
      parent.closeLocal();
      // parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    }
    parent.closeRemote();
    // parent_.upstream_conn_data_ = nullptr;
  }
}

void MySQLTerminalFilter::onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                                      Upstream::HostDescriptionConstSharedPtr) {
  canceler_ = nullptr;
  upstream_conn_data_ = std::move(conn);
  upstream_event_handler_ = std::make_unique<UpstreamEventHandler>(*this);
  upstream_conn_data_->addUpstreamCallbacks(*upstream_event_handler_);

  read_callbacks_->continueReading();
}

void MySQLTerminalFilter::onPoolFailure(Tcp::ConnectionPool::PoolFailureReason reason,
                                        Upstream::HostDescriptionConstSharedPtr host) {
  config_->stats_.login_failures_.inc();

  std::string host_info;
  if (host != nullptr) {
    host_info = " remote host address " + host->address()->asString();
  }
  switch (reason) {
  case Tcp::ConnectionPool::PoolFailureReason::Overflow:
    ENVOY_LOG(info, "mysql proxy upstream connection pool: too many connections, {}", host_info);
    break;
  case Tcp::ConnectionPool::PoolFailureReason::LocalConnectionFailure:
    ENVOY_LOG(info, "mysql proxy upstream connection pool:onocal connection failure, {}",
              host_info);
    break;
  case Tcp::ConnectionPool::PoolFailureReason::RemoteConnectionFailure:
    ENVOY_LOG(info, "mysql proxy upstream connection pool: remote connection failure, {}",
              host_info);
    break;
  case Tcp::ConnectionPool::PoolFailureReason::Timeout:
    ENVOY_LOG(info, "mysql proxy upstream connection pool: connection failure due to time out, {}",
              host_info);
    break;
  default:
    ENVOY_LOG(info, "mysql proxy upstream connection pool: unknown error, {}", host_info);
    break;
  }
  canceler_ = nullptr;
  read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

MySQLTerminalFilter::UpstreamEventHandler::UpstreamEventHandler(MySQLTerminalFilter& filter)
    : parent(filter) {}

void MySQLTerminalFilter::UpstreamEventHandler::onProtocolError() {
  parent.onProtocolError();
  ENVOY_LOG(info, "communication failure due to protocol error");
  parent.closeRemote();
  // read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

void MySQLTerminalFilter::UpstreamEventHandler::onNewMessage(MySQLSession::State state) {
  parent.onNewMessage(state);
  // close connection when received message on state NotHandled.
  if (state == MySQLSession::State::NotHandled || state == MySQLSession::State::Error) {
    ENVOY_LOG(info, "connection closed due to unexpected state occurs on communication");
    parent.closeRemote();
    // read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
  }
}

void MySQLTerminalFilter::UpstreamEventHandler::onServerGreeting(ServerGreeting& greet) {
  parent.onServerGreeting(greet);
  ENVOY_LOG(debug, "server {} send challenge", greet.getVersion());
}

void MySQLTerminalFilter::UpstreamEventHandler::onClientLoginResponse(
    ClientLoginResponse& login_resp) {
  parent.onClientLoginResponse(login_resp);
  if (login_resp.getRespCode() == MYSQL_RESP_ERR) {
    ENVOY_LOG(debug, "user failed to login into server, error message {}",
              dynamic_cast<ErrMessage&>(login_resp).getErrorMessage());
  }
  // sendLocal(login_resp);
}

void MySQLTerminalFilter::DownstreamEventHandler::onCommand(Command& command) {
  parent.onCommand(command);
  if (command.getCmd() == Command::Cmd::Quit) {

    return;
  }
}

void MySQLTerminalFilter::UpstreamEventHandler::onCommandResponse(CommandResponse& resp) {
  parent.onCommandResponse(resp);
}

Network::FilterStatus MySQLTerminalFilter::onData(Buffer::Instance& buffer, bool end_stream) {
  MySQLMonitorFilter::clearDynamicData();
  return downstream_event_handler_->onData(buffer, end_stream);
}

void MySQLTerminalFilter::sendLocal(MySQLCodec& message) {
  auto buffer = message.encodePacket();
  ENVOY_LOG(debug, "send data to client, len {}", buffer.length());
  read_callbacks_->connection().write(buffer, false);
}

void MySQLTerminalFilter::sendRemote(MySQLCodec& message) {
  auto buffer = message.encodePacket();
  ENVOY_LOG(debug, "send data to server, len {}", buffer.length());
  upstream_conn_data_->connection().write(buffer, false);
}

void MySQLTerminalFilter::UpstreamEventHandler::onUpstreamData(Buffer::Instance& data, bool) {
  ENVOY_LOG(debug, "upstream data recevied, len {}", buffer.length());
  buffer.move(data);
  decoder->onData(buffer);
}

Network::FilterStatus MySQLTerminalFilter::DownstreamEventHandler::onData(Buffer::Instance& data,
                                                                          bool) {
  ENVOY_LOG(debug, "downstream data recevied, len {}", buffer.length());
  buffer.move(data);
  decoder->onData(buffer);
  return Network::FilterStatus::StopIteration;
}

void MySQLTerminalFilter::stepLocalSession(uint8_t expected_seq,
                                           MySQLSession::State expected_state) {
  downstream_event_handler_->decoder->getSession().setExpectedSeq(expected_seq);
  downstream_event_handler_->decoder->getSession().setState(expected_state);
}

void MySQLTerminalFilter::stepRemoteSession(uint8_t expected_seq,
                                            MySQLSession::State expected_state) {
  upstream_event_handler_->decoder->getSession().setExpectedSeq(expected_seq);
  upstream_event_handler_->decoder->getSession().setState(expected_state);
}

void MySQLTerminalFilter::closeLocal() {
  if (read_callbacks_) {
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
  }
}

void MySQLTerminalFilter::closeRemote() {
  if (upstream_conn_data_) {
    upstream_conn_data_->connection().close(Network::ConnectionCloseType::NoFlush);
  }
}

void MySQLTerminalFilter::onFailure(const ClientLoginResponse& err) {
  auto buffer = err.encodePacket();
  read_callbacks_->connection().write(buffer, false);
}

void MySQLTerminalFilter::onAuthSucc() {
  ENVOY_LOG(debug, "downstream auth ok, wait for upstream connection ready");
  stepLocalSession(MYSQL_REQUEST_PKT_NUM, MySQLSession::State::Req);

  OkMessage ok = MessageHelper::encodeOk();
  ok.setSeq(MYSQL_LOGIN_RESP_PKT_NUM);
  sendLocal(ok);
}

absl::optional<ErrMessage>
MySQLTerminalFilter::DownstreamEventHandler::checkAuth(const std::string& name,
                                                       const std::vector<uint8_t>& login,
                                                       const std::vector<uint8_t>& expect_sig) {

  if (parent.downstream_username_ != name) {
    ENVOY_LOG(info, "filter: no such username {}", name);
    return MessageHelper::authError(
        name, parent.read_callbacks_->connection().addressProvider().remoteAddress()->asString(),
        true);
  }
  if (login.size() != expect_sig.size()) {
    ENVOY_LOG(info, "filter: password length error of client login, expected {}, got {}",
              expect_sig.size(), login.size());
    return MessageHelper::passwordLengthError(login.size());
  }
  if (expect_sig != login) {
    ENVOY_LOG(info, "filter: password is not correct");
    return MessageHelper::authError(
        name, parent.read_callbacks_->connection().addressProvider().remoteAddress()->asString(),
        true);
  }
  return absl::optional<ErrMessage>();
}

Network::FilterStatus MySQLTerminalFilter::onNewConnection() {
  MySQLMonitorFilter::onNewConnection();
  downstream_event_handler_->seed = AuthHelper::generateSeed();
  // send local packet
  auto packet = MessageHelper::encodeGreeting(downstream_event_handler_->seed);
  packet.setSeq(0);
  sendLocal(packet);
  stepLocalSession(1, MySQLSession::State::ChallengeResp41);
  return Network::FilterStatus::StopIteration;
}

void MySQLTerminalFilter::DownstreamEventHandler::onClientLogin(ClientLogin& login) {
  parent.onClientLogin(login);
  if (login.isSSLRequest()) {
    ENVOY_LOG(info, "communication failure due to proxy not support ssl upgrade");
    parent.closeLocal();
    return;
  }
  ENVOY_LOG(debug, "user {} try to login into database {}", login.getUsername(), login.getDb());

  auto auth_method = AuthHelper::authMethod(login.getClientCap(), login.getAuthPluginName());
  absl::optional<ErrMessage> err;
  switch (auth_method) {
  case AuthMethod::OldPassword:
    err = checkAuth(login.getUsername(), login.getAuthResp(),
                    OldPassword::signature(parent.downstream_password_, seed));

    break;
  case AuthMethod::NativePassword:
    auto err = checkAuth(login.getUsername(), login.getAuthResp(),
                         NativePassword::signature(parent.downstream_password_, seed));

    break;
  default:
    ENVOY_LOG(info, "auth plugin {} is not support", login.getAuthPluginName());
    auto auth_switch = MessageHelper::encodeAuthSwitch(seed);
    auth_switch.setSeq(login.getClientCap() + 1);
    parent.sendLocal(auth_switch);
    return;
  }
  if (err.has_value()) {
    err.value().setSeq(login.getSeq() + 1);
    parent.onFailure(err.value());
    return;
  }

  RouteSharedPtr route;
  if (login.getClientCap() & CLIENT_CONNECT_WITH_DB) {
    route = parent.router_->upstreamPool(login.getDb());
  } else {
    route = parent.router_->defaultPool();
  }
  if (route == nullptr) {
    ENVOY_LOG(info, "closed due to there is no cluster in route");
    parent.closeLocal();
    return;
  }

  auto cluster = route->upstream();
  if (cluster == nullptr) {
    ENVOY_LOG(info, "closed due to there is no cluster");
    parent.closeLocal();
    return;
  }

  auto pool = cluster->tcpConnPool(Upstream::ResourcePriority::Default, nullptr);

  if (!pool.has_value()) {
    ENVOY_LOG(info, "closed due to there is no host in cluster {}", cluster->info()->name());
    parent.closeLocal();
    return;
  }

  parent.canceler_ = pool->newConnection(parent);
}

void MySQLTerminalFilter::DownstreamEventHandler::onClientLoginResponse(ClientLoginResponse&) {
  ENVOY_LOG(error, "mysql filter: onMoreClientLoginResponse impossible callback is called");
}

void MySQLTerminalFilter::DownstreamEventHandler::onClientSwitchResponse(
    ClientSwitchResponse& switch_resp) {
  parent.onClientSwitchResponse(switch_resp);
  if checkPassword (switch_resp.)
}

void MySQLTerminalFilter::DownstreamEventHandler::onMoreClientLoginResponse(
    ClientLoginResponse& login_resp) {
  parent.onMoreClientLoginResponse(login_resp);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
