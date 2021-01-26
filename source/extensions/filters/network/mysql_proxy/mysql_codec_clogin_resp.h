#pragma once

#include "envoy/buffer/buffer.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

enum ClientLoginResponseType { Null, Ok, Err, AuthSwitch, AuthMoreData };

// ClientLoginResponse could be
// Protocol::OldAuthSwitchRequest, Protocol::AuthSwitchRequest when server want switch auth method
// or OK_Packet, ERR_Packet when server auth ok or error
class ClientLoginResponse : public MySQLCodec {
public:
  ClientLoginResponse() : type_(Null) {}
  ~ClientLoginResponse() override { cleanup(); }

  ClientLoginResponse(const ClientLoginResponse& other);                // copy constructor
  ClientLoginResponse(ClientLoginResponse&& other) noexcept;            // move constructor
  ClientLoginResponse& operator=(const ClientLoginResponse& other);     // copy assignment
  ClientLoginResponse& operator=(ClientLoginResponse&& other) noexcept; // move assignment
  bool operator==(const ClientLoginResponse& other) const; // test for equality, unit tests
  bool operator!=(const ClientLoginResponse& other) const { return !(*this == other); }

  // MySQLCodec
  int parseMessage(Buffer::Instance& buffer, uint32_t len) override;
  void encode(Buffer::Instance&) override;

  class AuthMoreMessage {
  public:
    AuthMoreMessage() = default;
    ~AuthMoreMessage() = default;
    AuthMoreMessage(const AuthMoreMessage&) = default;
    AuthMoreMessage(AuthMoreMessage&&) noexcept;
    AuthMoreMessage& operator=(const AuthMoreMessage&) = default;
    AuthMoreMessage& operator=(AuthMoreMessage&&) noexcept;
    bool operator==(const AuthMoreMessage&) const;
    std::string getAuthMoreData() const { return more_plugin_data_; }
    void setAuthMoreData(const std::string& data) { more_plugin_data_ = data; }
    friend ClientLoginResponse;

  private:
    std::string more_plugin_data_;
  };

  class AuthSwitchMessage {
  public:
    AuthSwitchMessage() = default;
    ~AuthSwitchMessage() = default;
    AuthSwitchMessage(const AuthSwitchMessage&);
    AuthSwitchMessage(AuthSwitchMessage&&) noexcept;
    AuthSwitchMessage& operator=(const AuthSwitchMessage&) = default;
    AuthSwitchMessage& operator=(AuthSwitchMessage&&) noexcept;
    bool operator==(const AuthSwitchMessage&) const;
    bool isOldAuthSwitch() const { return is_old_auth_switch_; }
    std::string getAuthPluginData() const { return auth_plugin_data_; }
    std::string getAuthPluginName() const { return auth_plugin_name_; }
    void setIsOldAuthSwitch(bool old) { is_old_auth_switch_ = old; }
    void setAuthPluginData(const std::string& data) { auth_plugin_data_ = data; }
    void setAuthPluginName(const std::string& name) { auth_plugin_name_ = name; }
    friend ClientLoginResponse;

  private:
    bool is_old_auth_switch_;
    std::string auth_plugin_data_;
    std::string auth_plugin_name_;
  };

  class OkMessage {
  public:
    OkMessage() = default;
    ~OkMessage() = default;
    OkMessage(const OkMessage&) = default;
    OkMessage(OkMessage&&) noexcept;
    OkMessage& operator=(const OkMessage&) = default;
    OkMessage& operator=(OkMessage&&) noexcept;
    bool operator==(const OkMessage&) const;
    void setAffectedRows(uint64_t affected_rows) { affected_rows_ = affected_rows; }
    void setLastInsertId(uint64_t last_insert_id) { last_insert_id_ = last_insert_id; }
    void setServerStatus(uint16_t status) { status_ = status; }
    void setWarnings(uint16_t warnings) { warnings_ = warnings; }
    void setInfo(const std::string& info) { info_ = info; }
    uint64_t getAffectedRows() const { return affected_rows_; }
    uint64_t getLastInsertId() const { return last_insert_id_; }
    uint16_t getServerStatus() const { return status_; }
    uint16_t getWarnings() const { return warnings_; }
    std::string getInfo() const { return info_; }
    friend ClientLoginResponse;

  private:
    uint64_t affected_rows_;
    uint64_t last_insert_id_;
    uint16_t status_;
    uint16_t warnings_;
    std::string info_;
  };

  class ErrMessage {
  public:
    ErrMessage() = default;
    ~ErrMessage() = default;
    ErrMessage(const ErrMessage&) = default;
    ErrMessage(ErrMessage&&) noexcept;
    ErrMessage& operator=(const ErrMessage&) = default;
    ErrMessage& operator=(ErrMessage&&) noexcept;
    bool operator==(const ErrMessage&) const;
    void setErrorCode(uint16_t error_code) { error_code_ = error_code; }
    void setSqlStateMarker(uint8_t marker) { marker_ = marker; }
    void setSqlState(const std::string& state) { sql_state_ = state; }
    void setErrorMessage(const std::string& msg) { error_message_ = msg; }
    uint16_t getErrorCode() const { return error_code_; }
    uint8_t getSqlStateMarker() const { return marker_; }
    std::string getSqlState() const { return sql_state_; }
    std::string getErrorMessage() const { return error_message_; }
    friend ClientLoginResponse;

  private:
    uint8_t marker_;
    uint16_t error_code_;
    std::string sql_state_;
    std::string error_message_;
  };
  const OkMessage& asOkMessage() const;
  OkMessage& asOkMessage();
  const ErrMessage& asErrMessage() const;
  ErrMessage& asErrMessage();
  const AuthSwitchMessage& asAuthSwitchMessage() const;
  AuthSwitchMessage& asAuthSwitchMessage();
  const AuthMoreMessage& asAuthMoreMessage() const;
  AuthMoreMessage& asAuthMoreMessage();

  /**
   * Get/set the type of the ClientLoginResponse. A ClientLoginResponse can only be a single type at
   * a time. Each time type() is called the type is changed and then the type specific as* methods
   * can be used.
   */
  ClientLoginResponseType type() const { return type_; }
  void type(ClientLoginResponseType);

private:
  int parseAuthSwitch(Buffer::Instance& buffer, uint32_t len);
  int parseOk(Buffer::Instance& buffer, uint32_t len);
  int parseErr(Buffer::Instance& buffer, uint32_t len);
  int parseAuthMore(Buffer::Instance& buffer, uint32_t len);
  void encodeAuthSwitch(Buffer::Instance&);
  void encodeOk(Buffer::Instance&);
  void encodeErr(Buffer::Instance&);
  void encodeAuthMore(Buffer::Instance&);

  void cleanup();

  ClientLoginResponseType type_{};
  union {
    AuthSwitchMessage auth_switch_;
    AuthMoreMessage auth_more_;
    ErrMessage err_;
    OkMessage ok_;
  };
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
