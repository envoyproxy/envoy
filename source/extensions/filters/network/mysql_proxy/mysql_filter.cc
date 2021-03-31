#include "extensions/filters/network/mysql_proxy/mysql_filter.h"

#include "envoy/api/api.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/outlier_detection.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/config/datasource.h"

#include "extensions/filters/network/mysql_proxy/conn_pool.h"
#include "extensions/filters/network/mysql_proxy/message_helper.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "extensions/filters/network/mysql_proxy/mysql_decoder_impl.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

MySQLFilterConfig::MySQLFilterConfig(
    Stats::Scope& scope,
    const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy& config, Api::Api& api)
    : stats_(generateStats(fmt::format("mysql.{}.", config.stat_prefix()), scope)),
      username_(Config::DataSource::read(config.downstream_auth_username(), true, api)),
      password_(Config::DataSource::read(config.downstream_auth_password(), true, api)) {}

MySQLFilter::MySQLFilter(MySQLFilterConfigSharedPtr config, RouterSharedPtr router,
                         ClientFactory& client_factory, DecoderFactory& decoder_factory)
    : config_(std::move(config)), decoder_(decoder_factory.create(*this)), router_(router),
      client_factory_(client_factory), decoder_factory_(decoder_factory), client_(nullptr) {}

void MySQLFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  read_callbacks_->connection().addConnectionCallbacks(*this);
}

void MySQLFilter::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    if (canceler_) {
      canceler_->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
      canceler_ = nullptr;
    }
    if (client_) {
      client_->close();
    }
  }
}

Network::FilterStatus MySQLFilter::onData(Buffer::Instance& data, bool) {
  ENVOY_LOG(debug, "downstream data sent, len {}", data.length());
  read_buffer_.move(data);
  if (client_ == nullptr && authed_) {
    return Network::FilterStatus::StopIteration;
  }
  doDecode(read_buffer_);
  return Network::FilterStatus::Continue;
}

void MySQLFilter::doDecode(Buffer::Instance& buffer) {
  // Clear dynamic metadata.
  envoy::config::core::v3::Metadata& dynamic_metadata =
      read_callbacks_->connection().streamInfo().dynamicMetadata();
  auto& metadata =
      (*dynamic_metadata.mutable_filter_metadata())[NetworkFilterNames::get().MySQLProxy];
  metadata.mutable_fields()->clear();

  try {
    decoder_->onData(buffer);
  } catch (EnvoyException& e) {
    ENVOY_LOG(info, "mysql_proxy: decoding error: {}", e.what());
    config_->stats_.decoder_errors_.inc();
    read_buffer_.drain(read_buffer_.length());
  }
}

void MySQLFilter::onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                              Upstream::HostDescriptionConstSharedPtr) {
  client_ = client_factory_.create(std::move(conn), decoder_factory_, *this);
  canceler_ = nullptr;
  read_callbacks_->continueReading();
  doDecode(read_buffer_);
  ENVOY_LOG(debug, "upstream client is ready, continue decoding");
}

void MySQLFilter::onPoolFailure(ConnPool::MySQLPoolFailureReason reason,
                                Upstream::HostDescriptionConstSharedPtr host) {
  config_->stats_.login_failures_.inc();

  std::string host_info;
  if (host != nullptr) {
    host_info = " remote host address " + host->address()->asString();
  }
  // triggers the release of the current stream at the end of the filter's callback.
  switch (reason) {
  case ConnPool::MySQLPoolFailureReason::Overflow:
    ENVOY_LOG(info, "mysql proxy upstream connection pool: too many connections, {}", host_info);
    break;
  case ConnPool::MySQLPoolFailureReason::LocalConnectionFailure:
    ENVOY_LOG(info, "mysql proxy upstream connection pool:onocal connection failure, {}",
              host_info);
    break;
  case ConnPool::MySQLPoolFailureReason::RemoteConnectionFailure:
    ENVOY_LOG(info, "mysql proxy upstream connection pool: remote connection failure, {}",
              host_info);
    break;
  case ConnPool::MySQLPoolFailureReason::Timeout:
    ENVOY_LOG(info, "mysql proxy upstream connection pool: connection failure due to time out, {}",
              host_info);
    break;
  case ConnPool::MySQLPoolFailureReason::AuthFailure:
    ENVOY_LOG(info, "mysql proxy upstream connection pool: connection failure due to auth, {}",
              host_info);
    break;
  case ConnPool::MySQLPoolFailureReason::ParseFailure:
    ENVOY_LOG(
        info,
        "mysql proxy upstream connection pool: connection failure due to error of parsing, {}",
        host_info);
    break;
  default:
    ENVOY_LOG(error, "mysql proxy upstream connection pool: unknown error, {}", host_info);
  }
  read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

void MySQLFilter::onResponse(MySQLCodec& codec, uint8_t seq) {
  auto buffer = MessageHelper::encodePacket(codec, seq);
  read_callbacks_->connection().write(buffer, false);
}

void MySQLFilter::onFailure() {
  ENVOY_LOG(error, "upstream client: proxy to server occur failure");
  read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

void MySQLFilter::onProtocolError() { config_->stats_.protocol_errors_.inc(); }

void MySQLFilter::onNewMessage(MySQLSession::State state) {
  if (state == MySQLSession::State::ChallengeReq) {
    config_->stats_.login_attempts_.inc();
  }
}

bool MySQLFilter::checkPassword(const ClientLogin& client_login, const std::vector<uint8_t>& seed,
                                size_t expect_length) {
  if (client_login.getAuthResp().size() != expect_length) {
    ENVOY_LOG(info, "filter: password length error of client login, expected {}, got {}",
              expect_length, client_login.getAuthResp().size());
    onFailure(MessageHelper::passwordLengthError(client_login.getAuthResp().size()), 2);
    return false;
  }
  if (AuthHelper::nativePasswordSignature(config_->password_, seed) != client_login.getAuthResp()) {
    ENVOY_LOG(info, "filter: password is not correct");
    onFailure(MessageHelper::authError(
                  client_login.getUsername(),
                  read_callbacks_->connection().addressProvider().remoteAddress()->asString(),
                  true),
              2);
    return false;
  }
  return true;
}

void MySQLFilter::onClientLogin(ClientLogin& client_login) {
  if (client_login.isSSLRequest()) {
    config_->stats_.upgraded_to_ssl_.inc();
    ENVOY_LOG(error, "client try to upgrade to ssl, which can not be handled");
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return;
  }
  if (config_->username_ != client_login.getUsername()) {
    ENVOY_LOG(info, "filter: no such username {}", client_login.getUsername());
    onFailure(MessageHelper::authError(
                  client_login.getUsername(),
                  read_callbacks_->connection().addressProvider().remoteAddress()->asString(),
                  true),
              2);
    return;
  }
  auto route = router_->upstreamPool(client_login.getDb());
  if (route == nullptr) {
    ENVOY_LOG(info, "filter: no corresponding upstream cluster for this db {}",
              client_login.getDb());
    onFailure(MessageHelper::dbError(client_login.getDb()), 2);
    return;
  }
  auto auth_method =
      AuthHelper::authMethod(client_login.getClientCap(), client_login.getAuthPluginName());
  switch (auth_method) {
  case AuthMethod::NativePassword:
    if (!checkPassword(client_login, seed_, NATIVE_PSSWORD_HASH_LENGTH)) {
      return;
    }
    break;
  case AuthMethod::OldPassword:
    if (!checkPassword(client_login, seed_, OLD_PASSWORD_HASH_LENGTH)) {
      return;
    }
    break;
  default: {
    auto auth_switch = MessageHelper::encodeAuthSwitch(seed_);
    auto buffer = MessageHelper::encodePacket(auth_switch, 2);
    read_callbacks_->connection().write(buffer, false);
    return;
  }
  }

  auto& pool = route->upstream();
  canceler_ = pool.newConnection(*this);
  onAuthOk();
}

void MySQLFilter::onAuthOk() {
  ENVOY_LOG(debug, "downstream auth ok, wait for upstream connection ready");
  authed_ = true;
  OkMessage ok = MessageHelper::encodeOk();
  auto buffer = MessageHelper::encodePacket(ok, MYSQL_LOGIN_RESP_PKT_NUM);
  decoder_->getSession().setExpectedSeq(MYSQL_REQUEST_PKT_NUM);
  decoder_->getSession().setState(MySQLSession::State::Req);
  read_callbacks_->connection().write(buffer, false);
}

void MySQLFilter::onFailure(const ClientLoginResponse& err, uint8_t seq) {
  auto buffer = MessageHelper::encodePacket(err, seq);
  read_callbacks_->connection().write(buffer, false);
}

void MySQLFilter::onClientLoginResponse(ClientLoginResponse&) {
  ENVOY_LOG(error, "mysql filter: onClientLoginResponse impossible callback is called");
}

void MySQLFilter::onMoreClientLoginResponse(ClientLoginResponse&) {
  ENVOY_LOG(error, "mysql filter: onMoreClientLoginResponse impossible callback is called");
}

void MySQLFilter::onCommand(Command& command) {
  ASSERT(client_ != nullptr);
  if (command.isQuery()) {
    envoy::config::core::v3::Metadata& dynamic_metadata =
        read_callbacks_->connection().streamInfo().dynamicMetadata();
    ProtobufWkt::Struct metadata(
        (*dynamic_metadata.mutable_filter_metadata())[NetworkFilterNames::get().MySQLProxy]);

    auto result = Common::SQLUtils::SQLUtils::setMetadata(command.getData(),
                                                          decoder_->getAttributes(), metadata);

    ENVOY_CONN_LOG(debug, "mysql_proxy: query processed {}", read_callbacks_->connection(),
                   command.getData());

    if (!result) {
      config_->stats_.queries_parse_error_.inc();
    } else {
      config_->stats_.queries_parsed_.inc();
      read_callbacks_->connection().streamInfo().setDynamicMetadata(
          NetworkFilterNames::get().MySQLProxy, metadata);
    }
  }
  if (command.getCmd() == Command::Cmd::Quit) {
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return;
  }
  // Parse a given query
  decoder_->getSession().setExpectedSeq(MYSQL_REQUEST_PKT_NUM);
  decoder_->getSession().setState(MySQLSession::State::Req);
  auto buffer = MessageHelper::encodePacket(command, MYSQL_REQUEST_PKT_NUM);
  client_->makeRequest(buffer);
}

Network::FilterStatus MySQLFilter::onNewConnection() {
  config_->stats_.sessions_.inc();
  seed_ = AuthHelper::generateSeed();
  auto greet = MessageHelper::encodeGreeting(seed_);
  Buffer::OwnedImpl buffer = MessageHelper::encodePacket(greet, GREETING_SEQ_NUM);
  decoder_->getSession().setExpectedSeq(GREETING_SEQ_NUM + 1);
  decoder_->getSession().setState(MySQLSession::State::ChallengeReq);
  read_callbacks_->connection().write(buffer, false);
  return Network::FilterStatus::Continue;
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
