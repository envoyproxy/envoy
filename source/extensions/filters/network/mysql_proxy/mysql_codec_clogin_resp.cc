#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"

#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"

#include "common/common/assert.h"
#include "common/common/logger.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

bool ClientLoginResponse::AuthSwitchMessage::operator==(
    const ClientLoginResponse::AuthSwitchMessage& other) const {
  return auth_plugin_data_ == other.auth_plugin_data_ &&
         auth_plugin_name_ == other.auth_plugin_name_;
}

bool ClientLoginResponse::AuthMoreMessage::operator==(
    const ClientLoginResponse::AuthMoreMessage& other) const {
  return more_plugin_data_ == other.more_plugin_data_;
}

bool ClientLoginResponse::OkMessage::operator==(const ClientLoginResponse::OkMessage& other) const {
  return affected_rows_ == other.affected_rows_ && last_insert_id_ == other.last_insert_id_ &&
         status_ == other.status_ && warnings_ == other.warnings_ && info_ == other.info_;
}

bool ClientLoginResponse::ErrMessage::operator==(const ErrMessage& other) const {
  return error_code_ == other.error_code_ && marker_ == other.marker_ &&
         sql_state_ == other.sql_state_ && error_message_ == other.error_message_;
}

void ClientLoginResponse::cleanup() {
  // free associate space
  switch (type_) {
  case Ok:
    ok_.reset(nullptr);
    break;
  case Err:
    err_.reset(nullptr);
    break;
  case AuthSwitch:
    auth_switch_.reset(nullptr);
    break;
  case AuthMoreData:
    auth_more_.reset(nullptr);
  default:
    break;
  }
}

void ClientLoginResponse::type(ClientLoginResponseType type) {
  if (type == type_) {
    return;
  }
  cleanup();
  // Need to use placement new because of the union.
  type_ = type;
  switch (type_) {
  case ClientLoginResponseType::Ok: {
    ok_ = std::make_unique<OkMessage>();
    break;
  }
  case ClientLoginResponseType::Err: {
    err_ = std::make_unique<ErrMessage>();
    break;
  }
  case ClientLoginResponseType::AuthSwitch: {
    auth_switch_ = std::make_unique<AuthSwitchMessage>();
    break;
  }
  case ClientLoginResponseType::AuthMoreData: {
    auth_more_ = std::make_unique<AuthMoreMessage>();
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
    *auth_switch_ = *(other.auth_switch_.get());
    break;
  case AuthMoreData:
    *auth_more_ = *(other.auth_more_.get());
    break;
  case Ok:
    *ok_ = *(other.ok_.get());
    break;
  case Err:
    *err_ = *(other.err_.get());
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
}

ClientLoginResponse& ClientLoginResponse::operator=(ClientLoginResponse&& other) noexcept {
  if (this == &other) {
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
ClientLoginResponse& ClientLoginResponse::operator=(const ClientLoginResponse& other) {
  if (this == &other) {
    return *this;
  }
  type(other.type_);
  switch (type_) {
  case Null:
    break;
  case AuthSwitch:
    *auth_switch_ = *(other.auth_switch_.get());
    break;
  case AuthMoreData:
    *auth_more_ = *(other.auth_more_.get());
    break;
  case Ok:
    *ok_ = *(other.ok_.get());
    break;
  case Err:
    *err_ = *(other.err_.get());
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
    return *ok_ == *other.ok_;
  case Err:
    return *err_ == *other.err_;
  case AuthSwitch:
    return *auth_switch_ == *other.auth_switch_;
  case AuthMoreData:
    return *auth_more_ == *other.auth_more_;
  default:
    return true;
  }
}

ClientLoginResponse::AuthMoreMessage& ClientLoginResponse::asAuthMoreMessage() {
  ASSERT(type_ == AuthMoreData);
  return *auth_more_;
}

ClientLoginResponse::AuthSwitchMessage& ClientLoginResponse::asAuthSwitchMessage() {
  ASSERT(type_ == AuthSwitch);
  return *auth_switch_;
}

ClientLoginResponse::OkMessage& ClientLoginResponse::asOkMessage() {
  ASSERT(type_ == Ok);
  return *ok_;
}

ClientLoginResponse::ErrMessage& ClientLoginResponse::asErrMessage() {
  ASSERT(type_ == Err);
  return *err_;
}

DecodeStatus ClientLoginResponse::parseMessage(Buffer::Instance& buffer, uint32_t len) {
  uint8_t resp_code;
  if (BufferHelper::readUint8(buffer, resp_code) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing response code in mysql Login response msg");
    return DecodeStatus::Failure;
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
  return DecodeStatus::Failure;
}

// https://dev.mysql.com/doc/internals/en/connection-phase-packets.html#packet-Protocol::AuthSwitchRequest
DecodeStatus ClientLoginResponse::parseAuthSwitch(Buffer::Instance& buffer, uint32_t) {
  // OldAuthSwitchRequest
  type(AuthSwitch);
  if (BufferHelper::endOfBuffer(buffer)) {
    auth_switch_->setIsOldAuthSwitch(true);
    return DecodeStatus::Success;
  }
  if (BufferHelper::readString(buffer, auth_switch_->auth_plugin_name_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing auth plugin name mysql Login response msg");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readStringEof(buffer, auth_switch_->auth_plugin_data_) !=
      DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing auth plugin data code in mysql Login Ok msg");
    return DecodeStatus::Failure;
  }
  auth_switch_->setIsOldAuthSwitch(false);
  return DecodeStatus::Success;
}

// https://dev.mysql.com/doc/internals/en/packet-OK_Packet.html
DecodeStatus ClientLoginResponse::parseOk(Buffer::Instance& buffer, uint32_t) {
  type(Ok);
  if (BufferHelper::readLengthEncodedInteger(buffer, ok_->affected_rows_) !=
      DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing affected_rows in mysql Login Ok msg");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readLengthEncodedInteger(buffer, ok_->last_insert_id_) !=
      DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing last_insert_id in mysql Login Ok msg");
    return DecodeStatus::Failure;
  }

  if (BufferHelper::readUint16(buffer, ok_->status_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing status in mysql Login Ok msg");
    return DecodeStatus::Failure;
  }
  // the exist of warning field is determined by server cap flag, but a decoder can not know the
  // cap flag, so just assume the CLIENT_PROTOCOL_41 is always set. ref
  // https://github.com/mysql/mysql-connector-j/blob/release/8.0/src/main/protocol-impl/java/com/mysql/cj/protocol/a/result/OkPacket.java#L48
  if (BufferHelper::readUint16(buffer, ok_->warnings_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing warnings in mysql Login Ok msg");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readStringEof(buffer, ok_->info_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing info in mysql Login Ok msg");
    return DecodeStatus::Failure;
  }
  return DecodeStatus::Success;
}

// https://dev.mysql.com/doc/internals/en/packet-ERR_Packet.html
DecodeStatus ClientLoginResponse::parseErr(Buffer::Instance& buffer, uint32_t) {
  type(Err);
  if (BufferHelper::readUint16(buffer, err_->error_code_) != MYSQL_RESP_OK) {
    ENVOY_LOG(info, "error parsing error code in mysql Login error msg");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readUint8(buffer, err_->marker_) != MYSQL_RESP_OK) {
    ENVOY_LOG(info, "error parsing sql state marker in mysql Login error msg");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readStringBySize(buffer, MYSQL_SQL_STATE_LEN, err_->sql_state_) !=
      MYSQL_RESP_OK) {
    ENVOY_LOG(info, "error parsing sql state in mysql Login error msg");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readStringEof(buffer, err_->error_message_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing error message in mysql Login error msg");
    return DecodeStatus::Failure;
  }
  return DecodeStatus::Success;
}

// https://dev.mysql.com/doc/internals/en/connection-phase-packets.html#packet-Protocol::AuthMoreData
DecodeStatus ClientLoginResponse::parseAuthMore(Buffer::Instance& buffer, uint32_t) {
  type(AuthMoreData);
  if (BufferHelper::readStringEof(buffer, auth_more_->more_plugin_data_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing more plugin data in mysql Login auth more msg");
    return DecodeStatus::Failure;
  }
  return DecodeStatus::Success;
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
  if (auth_switch_->isOldAuthSwitch()) {
    return;
  }
  BufferHelper::addString(out, auth_switch_->auth_plugin_name_);
  BufferHelper::addUint8(out, 0);
  BufferHelper::addString(out, auth_switch_->auth_plugin_data_);
  BufferHelper::addUint8(out, EOF);
}

void ClientLoginResponse::encodeOk(Buffer::Instance& out) {
  BufferHelper::addUint8(out, MYSQL_RESP_OK);
  BufferHelper::addLengthEncodedInteger(out, ok_->affected_rows_);
  BufferHelper::addLengthEncodedInteger(out, ok_->last_insert_id_);
  BufferHelper::addUint16(out, ok_->status_);
  BufferHelper::addUint16(out, ok_->warnings_);
  BufferHelper::addString(out, ok_->info_);
  BufferHelper::addUint8(out, EOF);
}

void ClientLoginResponse::encodeErr(Buffer::Instance& out) {
  BufferHelper::addUint8(out, MYSQL_RESP_ERR);
  BufferHelper::addUint16(out, err_->error_code_);
  BufferHelper::addUint8(out, err_->marker_);
  BufferHelper::addString(out, err_->sql_state_);
  BufferHelper::addString(out, err_->error_message_);
  BufferHelper::addUint8(out, EOF);
}

void ClientLoginResponse::encodeAuthMore(Buffer::Instance& out) {
  BufferHelper::addUint8(out, MYSQL_RESP_MORE);
  BufferHelper::addString(out, auth_more_->more_plugin_data_);
  BufferHelper::addUint8(out, EOF);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
