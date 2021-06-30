#include "source/extensions/filters/network/mysql_proxy/mysql_terminal_filter.h"

#include <memory>

#include "envoy/api/api.h"
#include "envoy/buffer/buffer.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/outlier_detection.h"
#include "envoy/upstream/upstream.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/config/datasource.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_config.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_decoder.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_decoder_impl.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_filter.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_message.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_session.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_utils.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "route.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

MySQLTerminalFilter::MySQLTerminalFilter(MySQLFilterConfigSharedPtr config, RouterSharedPtr router,
                                         DecoderFactory& factory, Api::Api& api)
    : MySQLMonitorFilter(config, factory), decoder_factory_(factory), router_(router),
      upstream_conn_data_(nullptr), api_(api) {
  downstream_event_handler_ = (std::make_unique<DownstreamEventHandler>(*this));
}

void MySQLTerminalFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  read_callbacks_->connection().addConnectionCallbacks(*this);
}

void MySQLTerminalFilter::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    ENVOY_LOG(debug, "mysql proxy: downstream connection closed");
    if (canceler_) {
      canceler_->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
      canceler_ = nullptr;
    }
    closeRemote();
    read_callbacks_ = nullptr;
  }
}

void MySQLTerminalFilter::UpstreamEventHandler::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    ENVOY_LOG(debug, "mysql proxy: upstream connection closed");
    parent.closeLocal();
    parent.upstream_conn_data_ = nullptr;
    parent.canceler_ = nullptr;
  }
}

void MySQLTerminalFilter::onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                                      Upstream::HostDescriptionConstSharedPtr) {
  canceler_ = nullptr;
  upstream_conn_data_ = std::move(conn);
  upstream_event_handler_ = std::make_unique<UpstreamEventHandler>(*this);
  upstream_conn_data_->addUpstreamCallbacks(*upstream_event_handler_);
}

void MySQLTerminalFilter::onPoolFailure(Tcp::ConnectionPool::PoolFailureReason reason,
                                        absl::string_view transport_failure_reason,
                                        Upstream::HostDescriptionConstSharedPtr host) {
  config_->stats_.login_failures_.inc();

  std::string host_info;
  if (host != nullptr) {
    host_info = " remote host address " + host->address()->asString();
  }
  switch (reason) {
  case Tcp::ConnectionPool::PoolFailureReason::Overflow:
    ENVOY_LOG(info,
              "mysql proxy upstream connection pool: too many connections, {}. Transport failure "
              "reason: {}.",
              host_info, transport_failure_reason);
    break;
  case Tcp::ConnectionPool::PoolFailureReason::LocalConnectionFailure:
    ENVOY_LOG(info,
              "mysql proxy upstream connection pool: local connection failure, {}. Transport "
              "failure reason: {}",
              host_info, transport_failure_reason);
    break;
  case Tcp::ConnectionPool::PoolFailureReason::RemoteConnectionFailure:
    ENVOY_LOG(info,
              "mysql proxy upstream connection pool: remote connection failure, {}. Transport "
              "failure reason: {}",
              host_info, transport_failure_reason);
    break;
  case Tcp::ConnectionPool::PoolFailureReason::Timeout:
    ENVOY_LOG(info,
              "mysql proxy upstream connection pool: connection failure due to time out, {}. "
              "Transport failure reason: {}",
              host_info, transport_failure_reason);
    break;
  default:
    ENVOY_LOG(
        info,
        "mysql proxy upstream connection pool: unknown error, {}. Transport failure reason: {}",
        host_info, transport_failure_reason);
    break;
  }
  canceler_ = nullptr;
  closeLocal();
}

void MySQLTerminalFilter::initUpstreamAuthInfo(Upstream::ThreadLocalCluster* cluster) {
  upstream_username_ = ProtocolOptionsConfigImpl::authUsername(cluster->info(), api_);
  upstream_password_ = ProtocolOptionsConfigImpl::authPassword(cluster->info(), api_);
}
void MySQLTerminalFilter::initDownstreamAuthInfo(const std::string& username,
                                                 const std::string& password) {
  downstream_username_ = username;
  downstream_password_ = password;
}

MySQLTerminalFilter::DownstreamEventHandler::DownstreamEventHandler(MySQLTerminalFilter& filter)
    : parent(filter), decoder(filter.decoder_factory_.create(*this)) {}

MySQLTerminalFilter::UpstreamEventHandler::UpstreamEventHandler(MySQLTerminalFilter& filter)
    : parent(filter), decoder(filter.decoder_factory_.create(*this)) {}

void MySQLTerminalFilter::DownstreamEventHandler::onProtocolError() {
  parent.onProtocolError();
  ENVOY_LOG(info, "mysql proxy: communication failure due to protocol error");
  parent.closeLocal();
}

void MySQLTerminalFilter::UpstreamEventHandler::onProtocolError() {
  parent.onProtocolError();
  ENVOY_LOG(info, "mysql proxy: communication failure due to protocol error");
  parent.closeRemote();
}

Network::FilterStatus MySQLTerminalFilter::onData(Buffer::Instance& buffer, bool end_stream) {
  MySQLMonitorFilter::clearDynamicData();
  return downstream_event_handler_->onData(buffer, end_stream);
}

void MySQLTerminalFilter::sendLocal(const MySQLCodec& message) {
  Buffer::OwnedImpl buffer;
  message.encodePacket(buffer);
  ENVOY_LOG(debug, "mysql proxy: send data to client, len {}", buffer.length());
  read_callbacks_->connection().write(buffer, false);
}

void MySQLTerminalFilter::sendRemote(const MySQLCodec& message) {
  Buffer::OwnedImpl buffer;
  message.encodePacket(buffer);
  ENVOY_LOG(debug, "mysql proxy: send data to server, len {}", buffer.length());
  upstream_conn_data_->connection().write(buffer, false);
}

void MySQLTerminalFilter::UpstreamEventHandler::onUpstreamData(Buffer::Instance& data, bool) {
  ENVOY_LOG(debug, "mysql proxy: upstream data recevied, len {}", data.length());
  buffer.move(data);
  decoder->onData(buffer);
}

Network::FilterStatus MySQLTerminalFilter::DownstreamEventHandler::onData(Buffer::Instance& data,
                                                                          bool) {
  ENVOY_LOG(debug, "mysql proxy: downstream data recevied, len {}", data.length());
  buffer.move(data);
  // wait upstream connection ready
  if (parent.upstream_event_handler_ != nullptr && !parent.upstream_event_handler_->ready) {
    return Network::FilterStatus::StopIteration;
  }
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
    // if there are any error information, we need to notify client
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }
}

void MySQLTerminalFilter::closeRemote() {
  if (upstream_conn_data_) {
    upstream_conn_data_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }
}

void MySQLTerminalFilter::onFailure(ErrMessage& err, uint8_t seq) {
  config_->stats_.login_failures_.inc();
  err.setSeq(seq);
  Buffer::OwnedImpl buffer;
  err.encodePacket(buffer);
  read_callbacks_->connection().write(buffer, false);
  closeLocal();
  closeRemote();
}

void MySQLTerminalFilter::DownstreamEventHandler::onBothAuthSucc() {
  ASSERT(pending_response.has_value());
  ENVOY_LOG(
      debug,
      "mysql proxy: downstream/upstream authentication success, notify client to command phase");
  parent.stepLocalSession(MYSQL_REQUEST_PKT_NUM, MySQLSession::State::Req);
  parent.sendLocal(pending_response.value());
  pending_response.reset();
}

absl::optional<ErrMessage>
MySQLTerminalFilter::DownstreamEventHandler::checkAuth(const std::string& name,
                                                       const std::vector<uint8_t>& sig,
                                                       const std::vector<uint8_t>& expect_sig) {

  if (parent.downstream_username_ != name) {
    ENVOY_LOG(info, "mysql proxy: no such user {}", name);
    return MessageHelper::authError(
        name, parent.read_callbacks_->connection().addressProvider().remoteAddress()->asString(),
        true);
  }
  if (sig.size() != expect_sig.size()) {
    ENVOY_LOG(info, "mysql proxy: password length error of client login, expected {}, got {}",
              expect_sig.size(), sig.size());
    return MessageHelper::passwordLengthError(sig.size());
  }
  if (expect_sig != sig) {
    ENVOY_LOG(info, "mysql proxy: password is not correct");
    return MessageHelper::authError(
        name, parent.read_callbacks_->connection().addressProvider().remoteAddress()->asString(),
        true);
  }
  return absl::optional<ErrMessage>();
}

Network::FilterStatus MySQLTerminalFilter::onNewConnection() {
  MySQLMonitorFilter::onNewConnection();
  downstream_event_handler_->seed = NativePassword::generateSeed();
  auto packet = MessageHelper::encodeGreeting(downstream_event_handler_->seed);
  packet.setSeq(0);
  sendLocal(packet);
  stepLocalSession(1, MySQLSession::State::ChallengeReq);
  return Network::FilterStatus::StopIteration;
}

void MySQLTerminalFilter::DownstreamEventHandler::onNewMessage(MySQLSession::State state) {
  parent.onNewMessage(state);
}

void MySQLTerminalFilter::DownstreamEventHandler::onServerGreeting(ServerGreeting&) {
  ENVOY_LOG(error,
            "mysql proxy: downstream decoder callback onMoreClientLoginResponse is not supported");
}

void MySQLTerminalFilter::DownstreamEventHandler::onClientLogin(ClientLogin& login) {
  parent.onClientLogin(login);

  if (login.isSSLRequest()) {
    ENVOY_LOG(info, "mysql proxy: communication failure due to lack of support for ssl upgrade");
    auto err = MessageHelper::defaultError("mysql proxy does not support ssl upgrade");
    parent.onFailure(err, login.getSeq() + 1);
    return;
  }
  ENVOY_LOG(debug, "mysql proxy: user {} try to login into database {}", login.getUsername(),
            login.getDb());

  auto auth_method = AuthHelper::authMethod(login.getClientCap(), login.getAuthPluginName());
  absl::optional<ErrMessage> err;
  parent.connect_db_ = login.getDb();
  switch (auth_method) {
  case AuthMethod::OldPassword:
    err = checkAuth(login.getUsername(), login.getAuthResp(),
                    OldPassword::signature(parent.downstream_password_, seed));

    break;
  case AuthMethod::NativePassword:
    err = checkAuth(login.getUsername(), login.getAuthResp(),
                    NativePassword::signature(parent.downstream_password_, seed));
    break;
  default:
    ENVOY_LOG(info, "mysql proxy: mysql proxy does not support auth plugin {}",
              login.getAuthPluginName());
    auto auth_switch = MessageHelper::encodeAuthSwitch(seed);
    auth_switch.setSeq(login.getClientCap() + 1);
    parent.sendLocal(auth_switch);
    parent.stepLocalSession(login.getClientCap() + 2, MySQLSession::State::AuthSwitchResp);
    return;
  }
  if (err.has_value()) {
    parent.onFailure(err.value(), login.getSeq() + 1);
    return;
  }
  tryConnectUpstream(login.getSeq() + 1);
}

void MySQLTerminalFilter::DownstreamEventHandler::onClientLoginResponse(ClientLoginResponse&) {
  ENVOY_LOG(error,
            "mysql proxy: downstream decoder callback onMoreClientLoginResponse is not supported.");
}

void MySQLTerminalFilter::DownstreamEventHandler::onClientSwitchResponse(
    ClientSwitchResponse& switch_resp) {
  parent.onClientSwitchResponse(switch_resp);
  auto err = checkAuth(parent.downstream_username_, switch_resp.getAuthPluginResp(),
                       NativePassword::signature(parent.downstream_password_, seed));
  if (err.has_value()) {
    parent.onFailure(err.value(), switch_resp.getSeq() + 1);
    return;
  }
  tryConnectUpstream(switch_resp.getSeq() + 1);
}

void MySQLTerminalFilter::DownstreamEventHandler::tryConnectUpstream(uint8_t seq) {
  RouteSharedPtr route = parent.router_->upstreamPool(parent.connect_db_);
  if (route == nullptr) {
    route = parent.router_->defaultPool();
  }
  if (route == nullptr) {
    ENVOY_LOG(info, "mysql proxy: close downstream due to no route found for database {}.",
              parent.connect_db_);
    auto err = MessageHelper::dbError(parent.connect_db_);
    parent.onFailure(err, seq);
    return;
  }
  auto cluster = route->upstream();
  if (cluster == nullptr) {
    ENVOY_LOG(info, "mysql proxy: close downstream due to cluster {} not found.", route->name());
    auto err = MessageHelper::defaultError(fmt::format("cluster {} is not found.", route->name()));
    parent.onFailure(err, seq);
    return;
  }
  auto pool = cluster->tcpConnPool(Upstream::ResourcePriority::Default, nullptr);

  if (!pool.has_value()) {
    ENVOY_LOG(info, "mysql proxy: close downstream due to no host found in cluster {}.",
              route->name());
    auto err =
        MessageHelper::defaultError(fmt::format("no host found in cluster {}.", route->name()));
    parent.onFailure(err, seq);
    return;
  }
  parent.initUpstreamAuthInfo(cluster);
  parent.canceler_ = pool->newConnection(parent);
  auto ok = MessageHelper::encodeOk();
  ok.setSeq(seq);
  // do not send ok to client immediately, wait upstream authentication success
  pending_response = ok;
}

void MySQLTerminalFilter::DownstreamEventHandler::onMoreClientLoginResponse(
    ClientLoginResponse& login_resp) {
  // envoy now not return AuthMoreData to client, this callback will never called.
  ENVOY_LOG(error,
            "mysql proxy: downstream decoder callback onMoreClientLoginResponse is not supported.");
  parent.onMoreClientLoginResponse(login_resp);
}

void MySQLTerminalFilter::DownstreamEventHandler::onCommand(Command& command) {
  ASSERT(parent.upstream_event_handler_ != nullptr);
  parent.onCommand(command);

  parent.sendRemote(command);

  if (command.getCmd() == Command::Cmd::Quit) {
    parent.closeLocal();
    return;
  }
  parent.stepLocalSession(MYSQL_REQUEST_PKT_NUM, MySQLSession::State::Req);
  parent.stepRemoteSession(MYSQL_RESPONSE_PKT_NUM, MySQLSession::State::ReqResp);
}

void MySQLTerminalFilter::DownstreamEventHandler::onCommandResponse(CommandResponse&) {
  ENVOY_LOG(error, "mysql proxy: downstream decoder callback onCommandResponse is not supported.");
}

void MySQLTerminalFilter::UpstreamEventHandler::onNewMessage(MySQLSession::State state) {
  parent.onNewMessage(state);
  // close connection when received message on state NotHandled.
  if (state == MySQLSession::State::NotHandled || state == MySQLSession::State::Error) {
    ENVOY_LOG(
        info,
        "mysql proxy: connection closed due to unexpected state encountered during communication.");
    parent.closeRemote();
  }
}

void MySQLTerminalFilter::UpstreamEventHandler::onServerGreeting(ServerGreeting& greet) {
  parent.onServerGreeting(greet);
  ENVOY_LOG(debug, "mysql proxy: server {} send challenge.", greet.getVersion());
  auto auth_method = AuthHelper::authMethod(greet.getServerCap(), greet.getAuthPluginName());
  if (auth_method != AuthMethod::OldPassword && auth_method != AuthMethod::NativePassword) {
    // If server's auth method is not supported, use mysql_native_password as response, wait for
    // auth switch.
    auth_method = AuthMethod::NativePassword;
  }
  seed = greet.getAuthPluginData();
  auto login = MessageHelper::encodeClientLogin(
      auth_method, parent.upstream_username_, parent.upstream_password_, parent.connect_db_, seed);
  login.setSeq(greet.getSeq() + 1);

  parent.sendRemote(login);
  parent.stepRemoteSession(greet.getSeq() + 2, MySQLSession::State::ChallengeResp41);
}

void MySQLTerminalFilter::UpstreamEventHandler::onClientLogin(ClientLogin&) {
  ENVOY_LOG(error, "mysql proxy: upstream decoder callback onClientLogin is not supported.");
}

void MySQLTerminalFilter::UpstreamEventHandler::onAuthSucc() {
  parent.downstream_event_handler_->onBothAuthSucc();
  ready = true;
  parent.stepRemoteSession(MYSQL_RESPONSE_PKT_NUM, MySQLSession::State::ReqResp);
}

void MySQLTerminalFilter::UpstreamEventHandler::onClientLoginResponse(
    ClientLoginResponse& login_resp) {
  parent.onClientLoginResponse(login_resp);

  absl::optional<ErrMessage> err;

  switch (login_resp.getRespCode()) {
  case MYSQL_RESP_AUTH_SWITCH: {
    auto& auth_switch = dynamic_cast<AuthSwitchMessage&>(login_resp);
    if (!auth_switch.isOldAuthSwitch() &&
        auth_switch.getAuthPluginName() != "mysql_native_password") {
      ENVOY_LOG(info,
                "mysql proxy: mysql proxy failed authenticate upstream server due to unsupported "
                "plugin {}. Only "
                "mysql_native_password and mysql_old_password are supported.",
                auth_switch.getAuthPluginName());
      err = MessageHelper::defaultError(fmt::format(
          "mysql proxy failed authenticate upstream server due to unsupported plugin {}. Only "
          "mysql_native_password and mysql_old_password are supported.",
          auth_switch.getAuthPluginName()));
      break;
    }
    auto auth_method =
        auth_switch.isOldAuthSwitch() ? AuthMethod::OldPassword : AuthMethod::NativePassword;
    seed = auth_switch.getAuthPluginData();

    auto switch_resp =
        MessageHelper::encodeClientLogin(auth_method, parent.upstream_username_,
                                         parent.upstream_password_, parent.connect_db_, seed);
    switch_resp.setSeq(login_resp.getSeq() + 1);
    parent.sendRemote(switch_resp);
    parent.stepRemoteSession(login_resp.getSeq() + 2, MySQLSession::State::AuthSwitchMore);
    break;
  }
  case MYSQL_RESP_OK:
    onAuthSucc();
    break;
  case MYSQL_RESP_ERR:
    ENVOY_LOG(info, "mysql proxy: mysql proxy failed authenticate upstream server. Error: {}",
              dynamic_cast<ErrMessage&>(login_resp).getErrorMessage());
    err = MessageHelper::defaultError(
        fmt::format("mysql proxy failed authenticate upstream server. Error: {}",
                    dynamic_cast<ErrMessage&>(login_resp).getErrorMessage()));
    break;
  case MYSQL_RESP_MORE:
  default:
    ENVOY_LOG(
        info,
        "mysql proxy: mysql proxy failed authenticate upstream server due to unhandled message "
        "resp code {}",
        login_resp.getRespCode());
    err = MessageHelper::defaultError(fmt::format(
        "mysql proxy failed authenticate upstream server due to unhandled message resp code {}",
        login_resp.getRespCode()));
    break;
  }
  if (err.has_value()) {
    // send error message to client when proxy auth error
    parent.onFailure(err.value(), parent.downstream_event_handler_->pending_response->getSeq());
  }
}

void MySQLTerminalFilter::UpstreamEventHandler::onClientSwitchResponse(ClientSwitchResponse&) {
  ENVOY_LOG(error,
            "mysql proxy: upstream decoder callback onClientSwitchResponse is not supported.");
}

void MySQLTerminalFilter::UpstreamEventHandler::onMoreClientLoginResponse(
    ClientLoginResponse& resp) {
  parent.onMoreClientLoginResponse(resp);

  absl::optional<ErrMessage> err;

  switch (resp.getRespCode()) {
  case MYSQL_RESP_MORE:
  case MYSQL_RESP_OK:
    onAuthSucc();
    break;
  case MYSQL_RESP_ERR:
    ENVOY_LOG(info, "mysql proxy: mysql proxy failed authenticate upstream server. Error: {}",
              dynamic_cast<ErrMessage&>(resp).getErrorMessage());
    err = MessageHelper::defaultError(
        fmt::format("mysql proxy failed authenticate upstream server. Error: {}",
                    dynamic_cast<ErrMessage&>(resp).getErrorMessage()));
    break;
  case MYSQL_RESP_AUTH_SWITCH:
  default:
    ENVOY_LOG(info,
              "mysql proxy: mysql proxy failed authenticate upstream server due to unhandled "
              "message resp code {}",
              resp.getRespCode());
    err = MessageHelper::defaultError(fmt::format(
        "mysql proxy failed authenticate upstream server due to unhandled message resp code {}",
        resp.getRespCode()));
    break;
  }
  if (err.has_value()) {
    parent.onFailure(err.value(), parent.downstream_event_handler_->pending_response->getSeq());
  }
}

void MySQLTerminalFilter::UpstreamEventHandler::onCommand(Command&) {
  ENVOY_LOG(error, "mysql proxy: upstream decoder callback onCommand is not supported.");
}

void MySQLTerminalFilter::UpstreamEventHandler::onCommandResponse(CommandResponse& resp) {
  parent.onCommandResponse(resp);
  parent.stepRemoteSession(resp.getSeq() + 1, MySQLSession::State::ReqResp);
  parent.sendLocal(resp);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
