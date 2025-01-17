#pragma once

#include <vector>

#include "envoy/buffer/buffer.h"

#include "source/common/buffer/buffer_impl.h"

#include "contrib/mysql_proxy/filters/network/source/mysql_codec.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_clogin.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class ClientLoginResponse;
class AuthMoreMessage;
class OkMessage;
class ErrMessage;
class AuthSwitchMessage;
using ClientLoginResponsePtr = std::unique_ptr<ClientLoginResponse>;

// ClientLoginResponse could be
// Protocol::OldAuthSwitchRequest, Protocol::AuthSwitchRequest when server wants switch auth
// method or OK_Packet, ERR_Packet when server auth ok or error
class ClientLoginResponse : public MySQLCodec {
public:
  ClientLoginResponse(uint8_t resp_code) : resp_code_(resp_code) {}
  // MySQLCodec
  uint8_t getRespCode() const { return resp_code_; }
  void setRespCode(uint8_t resp_code) { resp_code_ = resp_code; }

protected:
  uint8_t resp_code_;
};

class AuthMoreMessage : public ClientLoginResponse {
public:
  // ClientLoginResponse
  AuthMoreMessage() : ClientLoginResponse(MYSQL_RESP_MORE) {}
  DecodeStatus parseMessage(Buffer::Instance&, uint32_t) override;
  void encode(Buffer::Instance&) const override;
  const std::vector<uint8_t>& getAuthMoreData() const { return more_plugin_data_; }
  void setAuthMoreData(const std::vector<uint8_t>& data) { more_plugin_data_ = data; }

private:
  std::vector<uint8_t> more_plugin_data_;
};

class AuthSwitchMessage : public ClientLoginResponse {
public:
  AuthSwitchMessage() : ClientLoginResponse(MYSQL_RESP_AUTH_SWITCH) {}
  // ClientLoginResponse
  DecodeStatus parseMessage(Buffer::Instance&, uint32_t) override;
  void encode(Buffer::Instance&) const override;

  bool isOldAuthSwitch() const { return is_old_auth_switch_; }
  const std::vector<uint8_t>& getAuthPluginData() const { return auth_plugin_data_; }
  const std::string& getAuthPluginName() const { return auth_plugin_name_; }
  void setIsOldAuthSwitch(bool old) { is_old_auth_switch_ = old; }
  void setAuthPluginData(const std::vector<uint8_t>& data) { auth_plugin_data_ = data; }
  void setAuthPluginName(const std::string& name) { auth_plugin_name_ = name; }

private:
  bool is_old_auth_switch_{false};
  std::vector<uint8_t> auth_plugin_data_;
  std::string auth_plugin_name_;
};

class OkMessage : public ClientLoginResponse {
public:
  OkMessage() : ClientLoginResponse(MYSQL_RESP_OK) {}
  // ClientLoginResponse
  DecodeStatus parseMessage(Buffer::Instance&, uint32_t) override;
  void encode(Buffer::Instance&) const override;

  void setAffectedRows(uint64_t affected_rows) { affected_rows_ = affected_rows; }
  void setLastInsertId(uint64_t last_insert_id) { last_insert_id_ = last_insert_id; }
  void setServerStatus(uint16_t status) { status_ = status; }
  void setWarnings(uint16_t warnings) { warnings_ = warnings; }
  void setInfo(const std::string& info) { info_ = info; }
  uint64_t getAffectedRows() const { return affected_rows_; }
  uint64_t getLastInsertId() const { return last_insert_id_; }
  uint16_t getServerStatus() const { return status_; }
  uint16_t getWarnings() const { return warnings_; }
  const std::string& getInfo() const { return info_; }

private:
  uint64_t affected_rows_{0};
  uint64_t last_insert_id_{0};
  uint16_t status_{0};
  uint16_t warnings_{0};
  std::string info_;
  std::string session_state_changes_;
};

class ErrMessage : public ClientLoginResponse {
public:
  ErrMessage() : ClientLoginResponse(MYSQL_RESP_ERR) {}
  // ClientLoginResponse
  DecodeStatus parseMessage(Buffer::Instance&, uint32_t) override;
  void encode(Buffer::Instance&) const override;

  void setErrorCode(uint16_t error_code) { error_code_ = error_code; }
  void setSqlStateMarker(uint8_t marker) { marker_ = marker; }
  void setSqlState(const std::string& state) { sql_state_ = state; }
  void setErrorMessage(const std::string& msg) { error_message_ = msg; }
  uint16_t getErrorCode() const { return error_code_; }
  uint8_t getSqlStateMarker() const { return marker_; }
  const std::string& getSqlState() const { return sql_state_; }
  const std::string& getErrorMessage() const { return error_message_; }

private:
  uint8_t marker_{0};
  uint16_t error_code_{0};
  std::string sql_state_;
  std::string error_message_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
