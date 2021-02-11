#pragma once

#include "envoy/buffer/buffer.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

enum class ClientLoginResponseType : uint8_t {
  Null = 0,
  Ok = 1,
  Err = 2,
  AuthSwitch = 3,
  AuthMoreData = 4,
};

// ClientLoginResponse could be
// Protocol::OldAuthSwitchRequest, Protocol::AuthSwitchRequest when server wants switch auth
// method or OK_Packet, ERR_Packet when server auth ok or error
class ClientLoginResponse : public MySQLCodec {
public:
  ClientLoginResponse() : type_(ClientLoginResponseType::Null) {}
  ClientLoginResponse(const ClientLoginResponse&);

  // MySQLCodec
  DecodeStatus parseMessage(Buffer::Instance& buffer, uint32_t len) override;
  void encode(Buffer::Instance&) override;

  // Interface of MySQL client login response message
  class ClientLoginResponseMessage {
  public:
    using ClientLoginResponseMessagePtr = std::unique_ptr<ClientLoginResponseMessage>;
    virtual ~ClientLoginResponseMessage() = default;
    virtual DecodeStatus parseMessage(Buffer::Instance& buffer, uint32_t len) PURE;
    virtual void encode(Buffer::Instance& out) PURE;
  };

  class AuthMoreMessage : public ClientLoginResponseMessage {
  public:
    using AuthMoreMessagePtr = std::unique_ptr<AuthMoreMessage>;

    // ClientLoginResponseMessage
    DecodeStatus parseMessage(Buffer::Instance&, uint32_t) override;
    void encode(Buffer::Instance&) override;

    const std::string& getAuthMoreData() const { return more_plugin_data_; }
    void setAuthMoreData(const std::string& data) { more_plugin_data_ = data; }
    friend ClientLoginResponse;

  private:
    std::string more_plugin_data_;
  };

  class AuthSwitchMessage : public ClientLoginResponseMessage {
  public:
    using AuthSwitchMessagePtr = std::unique_ptr<AuthSwitchMessage>;

    // ClientLoginResponseMessage
    DecodeStatus parseMessage(Buffer::Instance&, uint32_t) override;
    void encode(Buffer::Instance&) override;

    bool isOldAuthSwitch() const { return is_old_auth_switch_; }
    const std::string& getAuthPluginData() const { return auth_plugin_data_; }
    const std::string& getAuthPluginName() const { return auth_plugin_name_; }
    void setIsOldAuthSwitch(bool old) { is_old_auth_switch_ = old; }
    void setAuthPluginData(const std::string& data) { auth_plugin_data_ = data; }
    void setAuthPluginName(const std::string& name) { auth_plugin_name_ = name; }
    friend ClientLoginResponse;

  private:
    bool is_old_auth_switch_;
    std::string auth_plugin_data_;
    std::string auth_plugin_name_;
  };

  class OkMessage : public ClientLoginResponseMessage {
  public:
    using OkMessagePtr = std::unique_ptr<OkMessage>;
    // ClientLoginResponseMessage
    DecodeStatus parseMessage(Buffer::Instance&, uint32_t) override;
    void encode(Buffer::Instance&) override;

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
    friend ClientLoginResponse;

  private:
    uint64_t affected_rows_;
    uint64_t last_insert_id_;
    uint16_t status_;
    uint16_t warnings_;
    std::string info_;
    std::string session_state_changes_;
  };

  class ErrMessage : public ClientLoginResponseMessage {
  public:
    using ErrMessagePtr = std::unique_ptr<ErrMessage>;

    // ClientLoginResponseMessage
    DecodeStatus parseMessage(Buffer::Instance&, uint32_t) override;
    void encode(Buffer::Instance&) override;

    void setErrorCode(uint16_t error_code) { error_code_ = error_code; }
    void setSqlStateMarker(uint8_t marker) { marker_ = marker; }
    void setSqlState(const std::string& state) { sql_state_ = state; }
    void setErrorMessage(const std::string& msg) { error_message_ = msg; }
    uint16_t getErrorCode() const { return error_code_; }
    uint8_t getSqlStateMarker() const { return marker_; }
    const std::string& getSqlState() const { return sql_state_; }
    const std::string& getErrorMessage() const { return error_message_; }
    friend ClientLoginResponse;

  private:
    uint8_t marker_;
    uint16_t error_code_;
    std::string sql_state_;
    std::string error_message_;
  };

  OkMessage& asOkMessage();
  ErrMessage& asErrMessage();
  AuthSwitchMessage& asAuthSwitchMessage();
  AuthMoreMessage& asAuthMoreMessage();

  /**
   * Get/set the type of the ClientLoginResponse. A ClientLoginResponse can only be a single type at
   * a time. Each time type() is called the type is changed and then the type specific as* methods
   * can be used.
   */
  ClientLoginResponseType type() const { return type_; }
  // init message_ from ClientLoginResponseType
  void type(ClientLoginResponseType type);

private:
  // init message_ from resp_code
  void type(uint8_t resp_code);

private:
  ClientLoginResponseType type_{};
  ClientLoginResponseMessage::ClientLoginResponseMessagePtr message_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
