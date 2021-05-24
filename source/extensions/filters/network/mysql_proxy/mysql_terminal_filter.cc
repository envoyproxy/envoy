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
#include "extensions/filters/network/mysql_proxy/mysql_filter.h"
#include "extensions/filters/network/mysql_proxy/mysql_session.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

MySQLTerminalFilter::MySQLTerminalFilter(MySQLFilterConfigSharedPtr config, RouterSharedPtr router)
    : MySQLMoniterFilter(config), router_(router), upstream_conn_data_(nullptr) {}

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
    if (parent_.read_callbacks_) {
      parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    }
    parent_.upstream_conn_data_ = nullptr;
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
    : parent_(filter) {}

void MySQLTerminalFilter::onProtocolError() {
  MySQLMoniterFilter::onProtocolError();
  ENVOY_LOG(info, "communication failure due to protocol error");
  read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

void MySQLTerminalFilter::onNewMessage(MySQLSession::State state) {
  MySQLMoniterFilter::onNewMessage(state);
  // close connection when received message on state NotHandled.
  if (state == MySQLSession::State::NotHandled || state == MySQLSession::State::Error) {
    ENVOY_LOG(info, "connection closed due to unexpected state occurs on communication");
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
  }
}

void MySQLTerminalFilter::onServerGreeting(ServerGreeting& greet) {
  MySQLMoniterFilter::onServerGreeting(greet);
  ENVOY_LOG(debug, "server {} send challenge", greet.getVersion());
  sendLocal(greet);
}

void MySQLTerminalFilter::onClientLogin(ClientLogin& client_login) {
  MySQLMoniterFilter::onClientLogin(client_login);
  if (client_login.isSSLRequest()) {
    ENVOY_LOG(info, "communication failure due to proxy not support ssl upgrade");
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return;
  }
  ENVOY_LOG(debug, "user {} try to login into database {}", client_login.getUsername(),
            client_login.getDb());
  sendRemote(client_login);
}

void MySQLTerminalFilter::onClientLoginResponse(ClientLoginResponse& client_login_resp) {
  MySQLMoniterFilter::onClientLoginResponse(client_login_resp);
  if (client_login_resp.getRespCode() == MYSQL_RESP_ERR) {
    ENVOY_LOG(debug, "user failed to login into server, error message {}",
              dynamic_cast<ErrMessage&>(client_login_resp).getErrorMessage());
  }
  sendLocal(client_login_resp);
}

void MySQLTerminalFilter::onClientSwitchResponse(ClientSwitchResponse& switch_resp) {
  MySQLMoniterFilter::onClientSwitchResponse(switch_resp);
  sendRemote(switch_resp);
}

void MySQLTerminalFilter::onMoreClientLoginResponse(ClientLoginResponse& client_login_resp) {
  MySQLMoniterFilter::onMoreClientLoginResponse(client_login_resp);
  sendLocal(client_login_resp);
}

void MySQLTerminalFilter::onCommand(Command& command) {
  MySQLMoniterFilter::onCommand(command);
  if (command.getCmd() == Command::Cmd::Quit) {
    sendRemote(command);
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return;
  }
  sendRemote(command);
}

void MySQLTerminalFilter::onCommandResponse(CommandResponse& resp) {
  MySQLMoniterFilter::onCommandResponse(resp);
  sendLocal(resp);
  // server response might contain more than one packets, so seq id expect to be seq + 1.
  stepSession(resp.getSeq() + 1, MySQLSession::State::ReqResp);
}

Network::FilterStatus MySQLTerminalFilter::onNewConnection() {
  MySQLMoniterFilter::onNewConnection();
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

Network::FilterStatus MySQLTerminalFilter::onData(Buffer::Instance& buffer, bool) {
  MySQLMoniterFilter::clearDynamicData();
  ENVOY_LOG(debug, "downstream data recevied, len {}", buffer.length());
  read_buffer_.move(buffer);
  decoder_->onData(read_buffer_);
  return Network::FilterStatus::StopIteration;
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

void MySQLTerminalFilter::UpstreamEventHandler::onUpstreamData(Buffer::Instance& buffer, bool) {
  ENVOY_LOG(debug, "upstream data sent, len {}", buffer.length());
  parent_.write_buffer_.move(buffer);
  parent_.decoder_->onData(parent_.write_buffer_);
}

void MySQLTerminalFilter::stepSession(uint8_t expected_seq, MySQLSession::State expected_state) {
  decoder_->getSession().setExpectedSeq(expected_seq);
  decoder_->getSession().setState(expected_state);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
