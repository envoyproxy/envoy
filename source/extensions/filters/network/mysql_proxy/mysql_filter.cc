#include "extensions/filters/network/mysql_proxy/mysql_filter.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/logger.h"

#include "extensions/filters/network/well_known_names.h"

#include "include/sqlparser/SQLParser.h"

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

Network::FilterStatus MySQLFilter::onData(Buffer::Instance& data, bool) {
  doDecode(data);
  return Network::FilterStatus::Continue;
}

Network::FilterStatus MySQLFilter::onWrite(Buffer::Instance& data, bool) {
  doDecode(data);
  return Network::FilterStatus::Continue;
}

void MySQLFilter::doDecode(Buffer::Instance& buffer) {
  // Safety measure just to make sure that if we have a decoding error we keep going and lose stats.
  // This can be removed once we are more confident of this code.
  if (!sniffing_) {
    buffer.drain(buffer.length());
    return;
  }

  // Clear dynamic metadata.
  auto& dynamic_metadata = const_cast<envoy::api::v2::core::Metadata&>(
      read_callbacks_->connection().streamInfo().dynamicMetadata());
  auto& metadata =
      (*dynamic_metadata.mutable_filter_metadata())[NetworkFilterNames::get().MySQLProxy];
  metadata.mutable_fields()->clear();

  if (!decoder_) {
    decoder_ = createDecoder(*this);
  }

  try {
    decoder_->onData(buffer);
  } catch (EnvoyException& e) {
    ENVOY_LOG(info, "mysql_proxy: decoding error: {}", e.what());
    config_->stats_.decoder_errors_.inc();
    sniffing_ = false;
  }
}

DecoderPtr MySQLFilter::createDecoder(DecoderCallbacks& callbacks) {
  return DecoderPtr{new DecoderImpl(callbacks, session_)};
}

void MySQLFilter::onProtocolError() { config_->stats_.protocol_errors_.inc(); }

void MySQLFilter::onLoginAttempt() { config_->stats_.login_attempts_.inc(); }

void MySQLFilter::decode(Buffer::Instance& message, uint64_t& offset, int seq, int len) {
  ENVOY_CONN_LOG(trace, "mysql_proxy: onData, len {}", read_callbacks_->connection(),
                 message.length());

  // Run the mysql state machine
  switch (session_.GetState()) {

  // expect Server Challenge packet
  case MySQLSession::State::MYSQL_INIT: {
    ServerGreeting greeting{};
    greeting.Decode(message, offset, seq, len);
    session_.SetState(MySQLSession::State::MYSQL_CHALLENGE_REQ);
    break;
  }

  // Process Client Handshake Response
  case MySQLSession::State::MYSQL_CHALLENGE_REQ: {
    ClientLogin client_login{};
    client_login.Decode(message, offset, seq, len);
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
    break;

  case MySQLSession::State::MYSQL_CHALLENGE_RESP_41:
  case MySQLSession::State::MYSQL_CHALLENGE_RESP_320: {
    ClientLoginResponse client_login_resp{};
    client_login_resp.Decode(message, offset, seq, len);
    if (client_login_resp.GetRespCode() == MYSQL_RESP_OK) {
      session_.SetState(MySQLSession::State::MYSQL_REQ);
    } else if (client_login_resp.GetRespCode() == MYSQL_RESP_AUTH_SWITCH) {
      config_->stats_.auth_switch_request_.inc();
      session_.SetState(MySQLSession::State::MYSQL_AUTH_SWITCH_RESP);
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
    client_switch_resp.Decode(message, offset, seq, len);
    session_.SetState(MySQLSession::State::MYSQL_AUTH_SWITCH_MORE);
    break;
  }

  case MySQLSession::State::MYSQL_AUTH_SWITCH_MORE: {
    ClientLoginResponse client_login_resp{};
    client_login_resp.Decode(message, offset, seq, len);
    if (client_login_resp.GetRespCode() == MYSQL_RESP_OK) {
      session_.SetState(MySQLSession::State::MYSQL_REQ);
    } else if (client_login_resp.GetRespCode() == MYSQL_RESP_MORE) {
      session_.SetState(MySQLSession::State::MYSQL_AUTH_SWITCH_RESP);
    } else if (client_login_resp.GetRespCode() == MYSQL_RESP_ERR) {
      config_->stats_.login_failures_.inc();
      session_.SetState(MySQLSession::State::MYSQL_ERROR);
    } else {
      session_.SetState(MySQLSession::State::MYSQL_NOT_HANDLED);
    }
    break;
  }

  // Process Command
  case MySQLSession::State::MYSQL_REQ: {
    Command command{};
    command.Decode(message, offset, seq, len);
    session_.SetState(MySQLSession::State::MYSQL_REQ_RESP);
    if (!command.RunQueryParser()) {
      // some mysql commands don't have a string to parse
      break;
    }
    // parse a given query
    hsql::SQLParserResult result;
    hsql::SQLParser::parse(command.GetData(), &result);

    ENVOY_CONN_LOG(trace, "mysql_proxy: msg processed {}", read_callbacks_->connection(),
                   command.GetData());

    // check whether the parsing was successful
    if (result.isValid()) {
      // Temporary until Venil's PR is merged.
      auto& dynamic_metadata = const_cast<envoy::api::v2::core::Metadata&>(
          read_callbacks_->connection().streamInfo().dynamicMetadata());

      ProtobufWkt::Struct metadata(
          (*dynamic_metadata.mutable_filter_metadata())[NetworkFilterNames::get().MySQLProxy]);
      auto& fields = *metadata.mutable_fields();

      for (auto i = 0u; i < result.size(); ++i) {
        hsql::TableAccessMap table_access_map;
        result.getStatement(i)->tablesAccessed(table_access_map);
        for (auto it = table_access_map.begin(); it != table_access_map.end(); ++it) {
          auto& operations = *fields[it->first].mutable_list_value();
          for (auto ot = it->second.begin(); ot != it->second.end(); ++ot) {
            operations.add_values()->set_string_value(*ot);
          }
        }
      }

      read_callbacks_->connection().streamInfo().setDynamicMetadata(
          NetworkFilterNames::get().MySQLProxy, metadata);
      // ProtobufTypes::String json;
      // Protobuf::util::MessageToJsonString(metadata, &json);
      // std::cout<<json<<'\n';
    }
    break;
  }

  // Process Command Response
  case MySQLSession::State::MYSQL_REQ_RESP: {
    CommandResp command_resp{};
    command_resp.Decode(message, offset, seq, len);
    session_.SetState(MySQLSession::State::MYSQL_REQ);
    break;
  }

  case MySQLSession::State::MYSQL_ERROR:
  case MySQLSession::State::MYSQL_NOT_HANDLED:
  default:
    break;
  }

  ENVOY_CONN_LOG(trace, "mysql_proxy: msg processed, session in state {}",
                 read_callbacks_->connection(), static_cast<int>(session_.GetState()));
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
