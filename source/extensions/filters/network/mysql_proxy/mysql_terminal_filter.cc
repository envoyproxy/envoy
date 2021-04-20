#include "extensions/filters/network/mysql_proxy/mysql_terminal_filter.h"

#include "envoy/api/api.h"
#include "envoy/buffer/buffer.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/outlier_detection.h"
#include "envoy/upstream/upstream.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/config/datasource.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "extensions/filters/network/mysql_proxy/mysql_decoder_impl.h"
#include "extensions/filters/network/mysql_proxy/mysql_session.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

MySQLTerminalFilter::MySQLTerminalFilter(MySQLFilterConfigSharedPtr config, RouterSharedPtr router,
                                         DecoderFactory& decoder_factory)
    : config_(std::move(config)), router_(router), decoder_factory_(decoder_factory),
      upstream_conn_data_(nullptr) {
  downstream_decoder_ = std::make_unique<DownstreamDecoder>(*this);
}

void MySQLTerminalFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  read_callbacks_->connection().addConnectionCallbacks(*downstream_decoder_);
}

void MySQLTerminalFilter::DownstreamDecoder::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    ENVOY_LOG(debug, "downstream connection closed");
    if (parent_.canceler_) {
      parent_.canceler_->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
      parent_.canceler_ = nullptr;
    }
    if (parent_.upstream_conn_data_) {
      parent_.upstream_conn_data_->connection().close(Network::ConnectionCloseType::NoFlush);
    }
    parent_.read_callbacks_ = nullptr;
  }
}

void MySQLTerminalFilter::UpstreamDecoder::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    ENVOY_LOG(debug, "upstream connection closed");
    if (parent_.read_callbacks_) {
      parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    }
    parent_.upstream_conn_data_ = nullptr;
  }
}

Network::FilterStatus MySQLTerminalFilter::onData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "downstream data sent, len {}", data.length());
  downstream_decoder_->onData(data, end_stream);
  return Network::FilterStatus::Continue;
}

void MySQLTerminalFilter::onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                                      Upstream::HostDescriptionConstSharedPtr) {
  canceler_ = nullptr;
  upstream_conn_data_ = std::move(conn);
  upstream_decoder_ = std::make_unique<UpstreamDecoder>(*this);
  upstream_conn_data_->addUpstreamCallbacks(*upstream_decoder_);

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

MySQLTerminalFilter::DownstreamDecoder::DownstreamDecoder(MySQLTerminalFilter& filter)
    : parent_(filter) {
  decoder_ = filter.decoder_factory_.create(*this);
}

MySQLTerminalFilter::UpstreamDecoder::UpstreamDecoder(MySQLTerminalFilter& filter)
    : parent_(filter) {
  decoder_ = filter.decoder_factory_.create(*this);
}

void MySQLTerminalFilter::DownstreamDecoder::onProtocolError() {
  ENVOY_LOG(info, "communication failure due to client protocol error");
  parent_.config_->stats_.protocol_errors_.inc();
  parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

void MySQLTerminalFilter::UpstreamDecoder::onProtocolError() {
  ENVOY_LOG(info, "communication failure due to server protocol error");
  parent_.config_->stats_.protocol_errors_.inc();
  parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

void MySQLTerminalFilter::DownstreamDecoder::onNewMessage(MySQLSession::State state) {
  // close connection when received message on state NotHandled.
  if (state == MySQLSession::State::NotHandled || state == MySQLSession::State::Error) {
    ENVOY_LOG(info,
              "connection closed due to unexpected state occurs on client -> proxy communication");
    parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
  }
}

void MySQLTerminalFilter::UpstreamDecoder::onNewMessage(MySQLSession::State state) {
  // close connection when received message on state NotHandled.
  if (state == MySQLSession::State::NotHandled || state == MySQLSession::State::Error) {
    ENVOY_LOG(
        info,
        "connection closed due to unexpected state occurs on proxy -> database communication");
    parent_.upstream_conn_data_->connection().close(Network::ConnectionCloseType::NoFlush);
  }
}

void MySQLTerminalFilter::UpstreamDecoder::onServerGreeting(ServerGreeting& greet) {
  ENVOY_LOG(debug, "server {} send challenge", greet.getVersion());
  send(greet);
  parent_.stepClientSession(greet.getSeq() + 1, MySQLSession::State::ChallengeReq);
}

void MySQLTerminalFilter::DownstreamDecoder::onClientLogin(ClientLogin& client_login) {
  ENVOY_LOG(debug, "user {} try to login into database {}", client_login.getUsername(),
            client_login.getDb());
  parent_.config_->stats_.login_attempts_.inc();
  if (client_login.isSSLRequest()) {
    parent_.config_->stats().upgraded_to_ssl_.inc();
    ENVOY_LOG(info, "communication failure due to proxy not support ssl upgrade");
    parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return;
  }
  send(client_login);
  parent_.stepServerSession(client_login.getSeq() + 1, MySQLSession::State::ChallengeResp41);
}

void MySQLTerminalFilter::UpstreamDecoder::onClientLoginResponse(
    ClientLoginResponse& client_login_resp) {
  // auth passed, step into command phase
  switch (client_login_resp.getRespCode()) {
  case MYSQL_RESP_OK:
    send(client_login_resp);
    ENVOY_LOG(debug, "login success");
    parent_.gotoCommandPhase();
    break;
  case MYSQL_RESP_AUTH_SWITCH:
    send(client_login_resp);
    parent_.config_->stats().auth_switch_request_.inc();
    parent_.stepClientSession(client_login_resp.getSeq() + 1, MySQLSession::State::AuthSwitchResp);
    break;
  // When resp code is MYSQL_RESP_ERR or MYSQL_RESP_MORE, client should close connection.
  case MYSQL_RESP_ERR:
    send(client_login_resp);
    parent_.config_->stats().login_failures_.inc();
    parent_.stepClientSession(client_login_resp.getSeq() + 1, MySQLSession::State::Error);
    break;
  case MYSQL_RESP_MORE:
  default:
    send(client_login_resp);
    parent_.stepClientSession(client_login_resp.getSeq() + 1, MySQLSession::State::NotHandled);
    break;
  }
}

void MySQLTerminalFilter::DownstreamDecoder::onClientSwitchResponse(
    ClientSwitchResponse& switch_resp) {
  send(switch_resp);
  parent_.stepServerSession(switch_resp.getSeq() + 1, MySQLSession::State::AuthSwitchMore);
}

void MySQLTerminalFilter::UpstreamDecoder::onMoreClientLoginResponse(
    ClientLoginResponse& client_login_resp) {
  switch (client_login_resp.getRespCode()) {
  case MYSQL_RESP_OK:
    send(client_login_resp);
    parent_.gotoCommandPhase();
    break;
  case MYSQL_RESP_MORE:
    send(client_login_resp);
    parent_.stepClientSession(client_login_resp.getSeq() + 1, MySQLSession::State::AuthSwitchResp);
    break;
    // When resp code is MYSQL_RESP_AUTH_SWITCH or MYSQL_RESP_ERR or others, client should close
    // connection.
  case MYSQL_RESP_ERR:
    send(client_login_resp);
    parent_.config_->stats().login_failures_.inc();
    parent_.stepClientSession(client_login_resp.getSeq() + 1, MySQLSession::State::Error);
    break;
  default:
    send(client_login_resp);
    parent_.stepClientSession(client_login_resp.getSeq() + 1, MySQLSession::State::NotHandled);
    break;
  }
}

void MySQLTerminalFilter::DownstreamDecoder::onCommand(Command& command) {
  if (command.isQuery()) {
    envoy::config::core::v3::Metadata& dynamic_metadata =
        parent_.read_callbacks_->connection().streamInfo().dynamicMetadata();
    ProtobufWkt::Struct metadata(
        (*dynamic_metadata.mutable_filter_metadata())[NetworkFilterNames::get().MySQLProxy]);

    auto result = Common::SQLUtils::SQLUtils::setMetadata(command.getData(),
                                                          decoder_->getAttributes(), metadata);

    ENVOY_CONN_LOG(debug, "mysql_proxy: query processed {}", parent_.read_callbacks_->connection(),
                   command.getData());

    if (!result) {
      parent_.config_->stats_.queries_parse_error_.inc();
    } else {
      parent_.config_->stats_.queries_parsed_.inc();
      parent_.read_callbacks_->connection().streamInfo().setDynamicMetadata(
          NetworkFilterNames::get().MySQLProxy, metadata);
    }
  }
  if (command.getCmd() == Command::Cmd::Quit) {
    send(command);
    parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return;
  }
  send(command);
  // client request will reset seq id
  parent_.gotoCommandPhase();
}

void MySQLTerminalFilter::UpstreamDecoder::onCommandResponse(CommandResponse& resp) {
  send(resp);
  // server response might contain more than one packets, so seq id expect to be seq + 1, wait for
  // client command request to reset server seq id.
  parent_.stepServerSession(resp.getSeq() + 1, MySQLSession::State::ReqResp);
}

void MySQLTerminalFilter::gotoCommandPhase() {
  // go to command phase, reset seq id
  stepServerSession(MYSQL_RESPONSE_PKT_NUM, MySQLSession::State::ReqResp);
  stepClientSession(MYSQL_REQUEST_PKT_NUM, MySQLSession::State::Req);
}

Network::FilterStatus MySQLTerminalFilter::onNewConnection() {
  config_->stats_.sessions_.inc();
  // we can not know the database name before connect to real backend database, so just connect to
  // catch all cluster backend. TODO(qinggniq) remove it in next pull request.
  auto primary_route = router_->defaultPool();
  if (primary_route == nullptr) {
    ENVOY_LOG(info, "closed due to there is no cluster in route");
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return Network::FilterStatus::StopIteration;
  }
  auto cluster = primary_route->upstream();
  if (cluster == nullptr) {
    ENVOY_LOG(info, "closed due to there is no cluster");
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return Network::FilterStatus::StopIteration;
  }

  auto pool = cluster->tcpConnPool(Upstream::ResourcePriority::Default, nullptr);

  if (pool == nullptr) {
    ENVOY_LOG(info, "closed due to there is no host in cluster {}", cluster->info()->name());
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return Network::FilterStatus::StopIteration;
  }
  canceler_ = pool->newConnection(*this);
  return Network::FilterStatus::StopIteration;
}

Network::FilterStatus MySQLTerminalFilter::DownstreamDecoder::onData(Buffer::Instance& buffer,
                                                                     bool) {
  // Clear dynamic metadata.
  envoy::config::core::v3::Metadata& dynamic_metadata =
      parent_.read_callbacks_->connection().streamInfo().dynamicMetadata();
  auto& metadata =
      (*dynamic_metadata.mutable_filter_metadata())[NetworkFilterNames::get().MySQLProxy];
  metadata.mutable_fields()->clear();

  buffer_.move(buffer);
  decoder_->onData(buffer_);
  return Network::FilterStatus::Continue;
}

void MySQLTerminalFilter::UpstreamDecoder::send(MySQLCodec& message) {
  auto buffer = message.encodePacket();
  ENVOY_LOG(debug, "send data to client, len {}", buffer.length());
  parent_.read_callbacks_->connection().write(buffer, false);
}

void MySQLTerminalFilter::DownstreamDecoder::send(MySQLCodec& message) {
  auto buffer = message.encodePacket();
  ENVOY_LOG(debug, "send data to server, len {}", buffer.length());
  parent_.upstream_conn_data_->connection().write(buffer, false);
}

void MySQLTerminalFilter::UpstreamDecoder::onUpstreamData(Buffer::Instance& buffer, bool) {
  ENVOY_LOG(debug, "upstream data sent, len {}", buffer.length());
  buffer_.move(buffer);
  decoder_->onData(buffer_);
}

void MySQLTerminalFilter::stepSession(MySQLSession& session, uint8_t expected_seq,
                                      MySQLSession::State expected_state) {
  session.setExpectedSeq(expected_seq);
  session.setState(expected_state);
}

void MySQLTerminalFilter::stepClientSession(uint8_t expected_seq,
                                            MySQLSession::State expected_state) {
  stepSession(downstream_decoder_->decoder_->getSession(), expected_seq, expected_state);
}

void MySQLTerminalFilter::stepServerSession(uint8_t expected_seq,
                                            MySQLSession::State expected_state) {
  stepSession(upstream_decoder_->decoder_->getSession(), expected_seq, expected_state);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
