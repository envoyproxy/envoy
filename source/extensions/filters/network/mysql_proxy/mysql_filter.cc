#include "extensions/filters/network/mysql_proxy/mysql_filter.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

MySQLFilterConfig::MySQLFilterConfig(const std::string& stat_prefix, Stats::Scope& scope)
    : scope_(scope), stat_prefix_(stat_prefix), stats_(generateStats(stat_prefix, scope)) {}

MySQLFilter::MySQLFilter(MySQLFilterConfigSharedPtr config) : config_(std::move(config)) {}

void MySQLFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
}

Network::FilterStatus MySQLFilter::onWrite(Buffer::Instance& data, bool end_stream) {
  return Process(data, end_stream);
}

Network::FilterStatus MySQLFilter::onData(Buffer::Instance& data, bool end_stream) {
  return Process(data, end_stream);
}

Network::FilterStatus MySQLFilter::Process(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(trace, "onData, len {}, end_stream {}", read_callbacks_->connection(),
                 data.length(), end_stream);
  if (!data.length()) {
    ENVOY_CONN_LOG(trace, "no data, return ", read_callbacks_->connection());
    return Network::FilterStatus::Continue;
  }

  // Run the mysql state machine
  switch (session_.GetState()) {

  // expect Server Challenge packet
  case MySQLSession::State::MYSQL_INIT: {
    ServerGreeting greeting{};
    greeting.Decode(data);
    if (greeting.GetSeq() != GREETING_SEQ_NUM) {
      config_->stats_.protocol_errors_.inc();
      break;
    }
    session_.SetState(MySQLSession::State::MYSQL_CHALLENGE_REQ);
    break;
  }

  // Process Client Handshake Response
  case MySQLSession::State::MYSQL_CHALLENGE_REQ: {
    config_->stats_.login_attempts_.inc();
    ClientLogin client_login{};
    client_login.Decode(data);
    if (client_login.GetSeq() != CHALLENGE_SEQ_NUM) {
      config_->stats_.protocol_errors_.inc();
      break;
    }
    if (client_login.IsSSLRequest()) {
      session_.SetState(MySQLSession::State::MYSQL_SSL_PT);
      config_->stats_.upgraded_to_ssl_.inc();
    } else if (client_login.IsResponse41()) {
      session_.SetState(MySQLSession::State::MYSQL_CHALLENGE_RESP_41);
    } else {
      session_.SetState(MySQLSession::State::MYSQL_CHALLENGE_RESP_320);
    }
    break;
  }

  case MySQLSession::State::MYSQL_SSL_PT:
    return Network::FilterStatus::Continue;

  case MySQLSession::State::MYSQL_CHALLENGE_RESP_41:
  case MySQLSession::State::MYSQL_CHALLENGE_RESP_320: {
    ClientLoginResponse client_login_resp{};
    client_login_resp.Decode(data);
    if (client_login_resp.GetSeq() != CHALLENGE_RESP_SEQ_NUM) {
      config_->stats_.protocol_errors_.inc();
      break;
    }
    if (client_login_resp.GetRespCode() == MYSQL_RESP_OK) {
      session_.SetState(MySQLSession::State::MYSQL_REQ);
    } else if (client_login_resp.GetRespCode() == MYSQL_RESP_AUTH_SWITCH) {
      config_->stats_.auth_switch_request_.inc();
      session_.SetState(MySQLSession::State::MYSQL_AUTH_SWITCH_RESP);
      session_.SetExpectedSeq(client_login_resp.GetSeq() + 1);
    } else if (client_login_resp.GetRespCode() == MYSQL_RESP_ERR) {
      config_->stats_.login_failures_.inc();
      session_.SetState(MySQLSession::State::MYSQL_ERROR);
    } else {
      session_.SetState(MySQLSession::State::MYSQL_NOT_HANDLED);
    }
    break;
  }

  case MySQLSession::State::MYSQL_AUTH_SWITCH_RESP: {
    ClientSwitchResponse client_switch_resp{};
    client_switch_resp.Decode(data);
    if ((client_switch_resp.GetSeq() != session_.GetExpectedSeq())) {
      config_->stats_.protocol_errors_.inc();
      break;
    }
    session_.SetState(MySQLSession::State::MYSQL_AUTH_SWITCH_MORE);
    session_.SetExpectedSeq(client_switch_resp.GetSeq() + 1);
    break;
  }

  case MySQLSession::State::MYSQL_AUTH_SWITCH_MORE: {
    ClientLoginResponse client_login_resp{};
    client_login_resp.Decode(data);
    if (client_login_resp.GetSeq() != session_.GetExpectedSeq()) {
      config_->stats_.protocol_errors_.inc();
      break;
    }
    if (client_login_resp.GetRespCode() == MYSQL_RESP_OK) {
      session_.SetState(MySQLSession::State::MYSQL_REQ);
    } else if (client_login_resp.GetRespCode() == MYSQL_RESP_MORE) {
      session_.SetState(MySQLSession::State::MYSQL_AUTH_SWITCH_RESP);
      session_.SetExpectedSeq(client_login_resp.GetSeq() + 1);
    } else if (client_login_resp.GetRespCode() == MYSQL_RESP_ERR) {
      config_->stats_.login_failures_.inc();
      session_.SetState(MySQLSession::State::MYSQL_ERROR);
    } else {
      session_.SetState(MySQLSession::State::MYSQL_NOT_HANDLED);
    }
    break;
  }

  // Process Query
  case MySQLSession::State::MYSQL_REQ:
    session_.SetState(MySQLSession::State::MYSQL_REQ_RESP);
    break;

  // Process Query Response
  case MySQLSession::State::MYSQL_REQ_RESP:
    session_.SetState(MySQLSession::State::MYSQL_REQ);
    break;

  case MySQLSession::State::MYSQL_ERROR:
  case MySQLSession::State::MYSQL_NOT_HANDLED:
  default:
    break;
  }

  ENVOY_CONN_LOG(trace, "mysql msg processed, session in state {}", read_callbacks_->connection(),
                 static_cast<int>(session_.GetState()));

  return Network::FilterStatus::Continue;
}

Network::FilterStatus MySQLFilter::onNewConnection() {
  config_->stats_.sessions_.inc();
  session_.SetId(read_callbacks_->connection().id());
  return Network::FilterStatus::Continue;
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
