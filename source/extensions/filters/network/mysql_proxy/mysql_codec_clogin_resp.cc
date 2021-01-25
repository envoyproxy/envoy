#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "envoy/buffer/buffer.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"
#include <bits/stdint-uintn.h>
#include <string>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

ClientLoginResponse::AuthSwitchMessage::AuthSwitchMessage(
    ClientLoginResponse::AuthSwitchMessage&& other) noexcept
    : auth_plugin_data_(std::move(other.auth_plugin_data_)),
      auth_plugin_name_(std::move(other.auth_plugin_name_)) {}

ClientLoginResponse::AuthSwitchMessage& ClientLoginResponse::AuthSwitchMessage::operator=(
    ClientLoginResponse::AuthSwitchMessage&& other) noexcept {
  if (&other == this) {
    return *this;
  }
  auth_plugin_data_ = std::move(other.auth_plugin_data_);
  auth_plugin_name_ = std::move(other.auth_plugin_name_);
  return *this;
}

bool ClientLoginResponse::AuthSwitchMessage::operator==(
    const ClientLoginResponse::AuthSwitchMessage& other) const {
  return auth_plugin_data_ == other.auth_plugin_data_ &&
         auth_plugin_name_ == other.auth_plugin_name_;
}

ClientLoginResponse::AuthMoreMessage::AuthMoreMessage(
    ClientLoginResponse::AuthMoreMessage&& other) noexcept
    : more_plugin_data_(std::move(other.more_plugin_data_)) {}

ClientLoginResponse::AuthMoreMessage& ClientLoginResponse::AuthMoreMessage::operator=(
    ClientLoginResponse::AuthMoreMessage&& other) noexcept {
  if (&other == this) {
    return *this;
  }
  more_plugin_data_ = std::move(other.more_plugin_data_);
  return *this;
}

bool ClientLoginResponse::AuthMoreMessage::operator==(
    const ClientLoginResponse::AuthMoreMessage& other) const {
  return more_plugin_data_ == other.more_plugin_data_;
}

ClientLoginResponse::OkMessage::OkMessage(ClientLoginResponse::OkMessage&& other) noexcept
    : affected_rows_(other.affected_rows_), last_insert_id_(other.last_insert_id_),
      status_(other.status_), warnings_(other.warnings_), info_(std::move(other.info_)) {}

ClientLoginResponse::OkMessage&
ClientLoginResponse::OkMessage::operator=(ClientLoginResponse::OkMessage&& other) noexcept {
  if (&other == this) {
    return *this;
  }
  affected_rows_ = other.affected_rows_;
  last_insert_id_ = other.last_insert_id_;
  status_ = other.status_;
  warnings_ = other.warnings_;
  info_ = std::move(other.info_);
  return *this;
}

bool ClientLoginResponse::OkMessage::operator==(const ClientLoginResponse::OkMessage& other) const {
  return affected_rows_ == other.affected_rows_ && last_insert_id_ == other.last_insert_id_ &&
         status_ == other.status_ && warnings_ == other.warnings_ && info_ == other.info_;
}

ClientLoginResponse::ErrMessage::ErrMessage(ClientLoginResponse::ErrMessage&& other) noexcept
    : marker_(other.marker_), error_code_(other.error_code_), sql_state_(other.sql_state_),
      error_message_(other.error_message_) {}

ClientLoginResponse::ErrMessage&
ClientLoginResponse::ErrMessage::operator=(ErrMessage&& other) noexcept {
  if (&other == this) {
    return *this;
  }
  error_code_ = other.error_code_;
  marker_ = other.marker_;
  sql_state_ = std::move(other.sql_state_);
  error_message_ = std::move(other.error_message_);
  return *this;
}

bool ClientLoginResponse::ErrMessage::operator==(const ErrMessage& other) const {
  return error_code_ == other.error_code_ && marker_ == other.marker_ &&
         sql_state_ == other.sql_state_ && error_message_ == other.error_message_;
}

void ClientLoginResponse::cleanup() {
  // Need to manually delete because of the union.
  switch (type_) {
  case Ok:
    ok_.~OkMessage();
    break;
  case Err:
    err_.~ErrMessage();
    break;
  case AuthSwitch:
    auth_switch_.~AuthSwitchMessage();
    break;
  case AuthMoreData:
    auth_more_.~AuthMoreMessage();
  default:
    break;
  }
}

void ClientLoginResponse::type(ClientLoginResponseType type) {
  cleanup();
  // Need to use placement new because of the union.
  type_ = type;
  switch (type_) {
  case ClientLoginResponseType::Ok: {
    new (&ok_) OkMessage();
    break;
  }
  case ClientLoginResponseType::Err: {
    new (&err_) ErrMessage();
    break;
  }
  case ClientLoginResponseType::AuthSwitch: {
    new (&auth_switch_) AuthSwitchMessage();
    break;
  }
  case ClientLoginResponseType::AuthMoreData: {
    new (&auth_more_) AuthMoreMessage();
    break;
  }
  default:
    break;
  }
}

ClientLoginResponse::ClientLoginResponse(const ClientLoginResponse& other)
    : type_(ClientLoginResponseType::Null) {
  type(other.type_);
  switch (type_) {
  case Null:
    break;
  case AuthSwitch:
    asAuthSwitchMessage() = other.asAuthSwitchMessage();
    break;
  case AuthMoreData:
    asAuthMoreMessage() = other.asAuthMoreMessage();
    break;
  case Ok:
    asOkMessage() = other.asOkMessage();
    break;
  case Err:
    asErrMessage() = other.asErrMessage();
    break;
  default:
    break;
  }
}
ClientLoginResponse::ClientLoginResponse(ClientLoginResponse&& other) noexcept {
  type(other.type_);
  switch (type_) {
  case Null:
    break;
  case AuthSwitch:
    new (&auth_switch_) AuthSwitchMessage(std::move(other.auth_switch_));
    break;
  case AuthMoreData:
    new (&auth_more_) AuthMoreMessage(std::move(other.auth_more_));
    break;
  case Ok:
    new (&ok_) OkMessage(std::move(other.ok_));
    break;
  case Err:
    new (&err_) ErrMessage(std::move(other.err_));
    break;
  default:
    break;
  }
}

ClientLoginResponse& ClientLoginResponse::operator=(const ClientLoginResponse& other) {
  if (&other == this) {
    return *this;
  }

  type(other.type_);
  switch (type_) {
  case Null:
    break;
  case AuthSwitch:
    asAuthSwitchMessage() = other.asAuthSwitchMessage();
    break;
  case AuthMoreData:
    asAuthMoreMessage() = other.asAuthMoreMessage();
    break;
  case Ok:
    asOkMessage() = other.asOkMessage();
    break;
  case Err:
    asErrMessage() = other.asErrMessage();
    break;
  default:
    break;
  }
  return *this;
}
ClientLoginResponse& ClientLoginResponse::operator=(ClientLoginResponse&& other) noexcept {
  if (&other == this) {
    return *this;
  }
  type(other.type_);
  switch (type_) {
  case Null:
    break;
  case AuthSwitch:
    auth_switch_ = std::move(other.auth_switch_);
    break;
  case AuthMoreData:
    auth_more_ = std::move(other.auth_more_);
    break;
  case Ok:
    ok_ = std::move(other.ok_);
    break;
  case Err:
    err_ = std::move(other.err_);
    break;
  default:
    break;
  }
  return *this;
}

bool ClientLoginResponse::operator==(const ClientLoginResponse& other) const {
  if (&other == this) {
    return true;
  }
  if (other.type_ != type_) {
    return false;
  }
  switch (type_) {
  case Ok:
    return ok_ == other.ok_;
  case Err:
    return err_ == other.err_;
  case AuthSwitch:
    return auth_switch_ == other.auth_switch_;
  case AuthMoreData:
    return auth_more_ == other.auth_more_;
  default:
    return true;
  }
}

ClientLoginResponse::AuthMoreMessage& ClientLoginResponse::asAuthMoreMessage() {
  ASSERT(type_ == AuthMoreData);
  return auth_more_;
}

const ClientLoginResponse::AuthMoreMessage& ClientLoginResponse::asAuthMoreMessage() const {
  ASSERT(type_ == AuthMoreData);
  return auth_more_;
}

ClientLoginResponse::AuthSwitchMessage& ClientLoginResponse::asAuthSwitchMessage() {
  ASSERT(type_ == AuthSwitch);
  return auth_switch_;
}

const ClientLoginResponse::AuthSwitchMessage& ClientLoginResponse::asAuthSwitchMessage() const {
  ASSERT(type_ == AuthSwitch);
  return auth_switch_;
}

ClientLoginResponse::OkMessage& ClientLoginResponse::asOkMessage() {
  ASSERT(type_ == Ok);
  return ok_;
}
const ClientLoginResponse::OkMessage& ClientLoginResponse::asOkMessage() const {
  ASSERT(type_ == Ok);
  return ok_;
}

ClientLoginResponse::ErrMessage& ClientLoginResponse::asErrMessage() {
  ASSERT(type_ == Err);
  return err_;
}
const ClientLoginResponse::ErrMessage& ClientLoginResponse::asErrMessage() const {
  ASSERT(type_ == Err);
  return err_;
}

int ClientLoginResponse::parseMessage(Buffer::Instance& buffer, uint32_t len) {
  uint8_t resp_code;
  if (BufferHelper::readUint8(buffer, resp_code) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing response code in mysql Login response msg");
    return MYSQL_FAILURE;
  }
  switch (resp_code) {
  case MYSQL_RESP_AUTH_SWITCH:
    return parseAuthSwitch(buffer, len);
  case MYSQL_RESP_OK:
    return parseOk(buffer, len);
  case MYSQL_RESP_ERR:
    return parseErr(buffer, len);
  case MYSQL_RESP_MORE:
    return parseAuthMore(buffer, len);
  }
  ENVOY_LOG(info, "unknown mysql Login resp msg type");
  return MYSQL_FAILURE;
}

// https://dev.mysql.com/doc/internals/en/connection-phase-packets.html#packet-Protocol::AuthSwitchRequest
int ClientLoginResponse::parseAuthSwitch(Buffer::Instance& buffer, uint32_t) {
  // OldAuthSwitchRequest
  type(AuthSwitch);
  if (BufferHelper::endOfBuffer(buffer)) {
    auth_switch_.setIsOldAuthSwitch(true);
    return MYSQL_SUCCESS;
  }
  if (BufferHelper::readString(buffer, auth_switch_.auth_plugin_name_) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing auth plugin name mysql Login response msg");
    return MYSQL_FAILURE;
  }
  if (BufferHelper::readStringEof(buffer, auth_switch_.auth_plugin_data_) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing auth plugin data code in mysql Login Ok msg");
    return MYSQL_FAILURE;
  }
  auth_switch_.setIsOldAuthSwitch(false);
  return MYSQL_SUCCESS;
}

// https://dev.mysql.com/doc/internals/en/packet-OK_Packet.html
int ClientLoginResponse::parseOk(Buffer::Instance& buffer, uint32_t) {
  type(Ok);
  if (BufferHelper::readLengthEncodedInteger(buffer, ok_.affected_rows_) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing affected_rows in mysql Login Ok msg");
    return MYSQL_FAILURE;
  }
  if (BufferHelper::readLengthEncodedInteger(buffer, ok_.last_insert_id_) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing last_insert_id in mysql Login Ok msg");
    return MYSQL_FAILURE;
  }

  if (BufferHelper::readUint16(buffer, ok_.status_) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing status in mysql Login Ok msg");
    return MYSQL_FAILURE;
  }
  // the exist of warning feild is determined by server cap flag, but a decoder can not know the
  // cap flag, so just assume the CLIENT_PROTOCOL_41 is always set. ref
  // https://github.com/mysql/mysql-connector-j/blob/release/8.0/src/main/protocol-impl/java/com/mysql/cj/protocol/a/result/OkPacket.java#L48
  if (BufferHelper::readUint16(buffer, ok_.warnings_) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing warnings in mysql Login Ok msg");
    return MYSQL_FAILURE;
  }
  if (BufferHelper::readString(buffer, ok_.info_) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing info in mysql Login Ok msg");
    return MYSQL_FAILURE;
  }
  return MYSQL_SUCCESS;
}

// https://dev.mysql.com/doc/internals/en/packet-ERR_Packet.html
int ClientLoginResponse::parseErr(Buffer::Instance& buffer, uint32_t) {
  type(Err);
  if (BufferHelper::readUint16(buffer, err_.error_code_) != MYSQL_RESP_OK) {
    ENVOY_LOG(info, "error parsing error code in mysql Login error msg");
    return MYSQL_FAILURE;
  }
  if (BufferHelper::readUint8(buffer, err_.marker_) != MYSQL_RESP_OK) {
    ENVOY_LOG(info, "error parsing sql state marker in mysql Login error msg");
    return MYSQL_FAILURE;
  }
  if (BufferHelper::readStringBySize(buffer, MYSQL_SQL_STATE_LEN, err_.sql_state_) !=
      MYSQL_RESP_OK) {
    ENVOY_LOG(info, "error parsing sql state in mysql Login error msg");
    return MYSQL_FAILURE;
  }
  if (BufferHelper::readStringEof(buffer, err_.error_message_) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing error message in mysql Login error msg");
    return MYSQL_FAILURE;
  }
  return MYSQL_SUCCESS;
}

// https://dev.mysql.com/doc/internals/en/connection-phase-packets.html#packet-Protocol::AuthMoreData
int ClientLoginResponse::parseAuthMore(Buffer::Instance& buffer, uint32_t) {
  type(AuthMoreData);
  if (BufferHelper::readStringEof(buffer, auth_more_.more_plugin_data_) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing more plugin data in mysql Login auth more msg");
    return MYSQL_FAILURE;
  }
  return MYSQL_SUCCESS;
}

void ClientLoginResponse::encode(Buffer::Instance& out) {
  switch (type_) {
  case AuthSwitch:
    encodeAuthSwitch(out);
    break;
  case Ok:
    encodeOk(out);
    break;
  case Err:
    encodeErr(out);
    break;
  case AuthMoreData:
    encodeAuthMore(out);
    break;
  default:
    break;
  }
}

void ClientLoginResponse::encodeAuthSwitch(Buffer::Instance& out) {
  BufferHelper::addUint8(out, MYSQL_RESP_AUTH_SWITCH);
  if (auth_switch_.isOldAuthSwitch()) {
    return;
  }
  BufferHelper::addString(out, auth_switch_.auth_plugin_name_);
  BufferHelper::addUint8(out, 0);
  BufferHelper::addString(out, auth_switch_.auth_plugin_data_);
  BufferHelper::addUint8(out, EOF);
}

void ClientLoginResponse::encodeOk(Buffer::Instance& out) {
  BufferHelper::addUint8(out, MYSQL_RESP_OK);
  BufferHelper::addLengthEncodedInteger(out, ok_.affected_rows_);
  BufferHelper::addLengthEncodedInteger(out, ok_.last_insert_id_);
  BufferHelper::addUint16(out, ok_.status_);
  BufferHelper::addUint16(out, ok_.warnings_);
  BufferHelper::addString(out, ok_.info_);
  BufferHelper::addUint8(out, EOF);
}

void ClientLoginResponse::encodeErr(Buffer::Instance& out) {
  BufferHelper::addUint8(out, MYSQL_RESP_ERR);
  BufferHelper::addUint16(out, err_.error_code_);
  BufferHelper::addUint8(out, err_.marker_);
  BufferHelper::addString(out, err_.sql_state_);
  BufferHelper::addString(out, err_.error_message_);
  BufferHelper::addUint8(out, EOF);
}

void ClientLoginResponse::encodeAuthMore(Buffer::Instance& out) {
  BufferHelper::addUint8(out, MYSQL_RESP_MORE);
  BufferHelper::addString(out, auth_more_.more_plugin_data_);
  BufferHelper::addUint8(out, EOF);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
