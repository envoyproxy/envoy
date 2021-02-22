#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"

#include <iostream>
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
void ClientLoginResponse::initMessage(uint8_t resp_code) {
  resp_code_ = resp_code;
  switch (resp_code) {
  case MYSQL_RESP_OK:
    message_ = std::make_unique<OkMessage>();
    break;
  case MYSQL_RESP_ERR:
    message_ = std::make_unique<ErrMessage>();
    break;
  case MYSQL_RESP_AUTH_SWITCH:
    message_ = std::make_unique<AuthSwitchMessage>();
    break;
  case MYSQL_RESP_MORE:
    message_ = std::make_unique<AuthMoreMessage>();
    break;
  default:
    break;
  }
}

AuthMoreMessage& ClientLoginResponse::asAuthMoreMessage() {
  ASSERT(resp_code_ == MYSQL_RESP_MORE);
  return *(dynamic_cast<AuthMoreMessage*>(message_.get()));
}
AuthSwitchMessage& ClientLoginResponse::asAuthSwitchMessage() {
  ASSERT(resp_code_ == MYSQL_RESP_AUTH_SWITCH);
  return *(dynamic_cast<AuthSwitchMessage*>(message_.get()));
}

OkMessage& ClientLoginResponse::asOkMessage() {
  ASSERT(resp_code_ == MYSQL_RESP_OK);
  return *(dynamic_cast<OkMessage*>(message_.get()));
}
ErrMessage& ClientLoginResponse::asErrMessage() {
  ASSERT(resp_code_ == MYSQL_RESP_ERR);
  return *(dynamic_cast<ErrMessage*>(message_.get()));
}

DecodeStatus ClientLoginResponse::parseMessage(Buffer::Instance& buffer, uint32_t len) {
  if (BufferHelper::readUint8(buffer, resp_code_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing response code in mysql Login response msg");
    return DecodeStatus::Failure;
  }
  initMessage(resp_code_);
  if (message_ == nullptr) {
    ENVOY_LOG(info, "unknown mysql Login resp code");
    return DecodeStatus::Failure;
  }
  return message_->parseMessage(buffer, len - sizeof(uint8_t));
}

// https://dev.mysql.com/doc/internals/en/connection-phase-packets.html#packet-Protocol::AuthSwitchRequest
DecodeStatus AuthSwitchMessage::parseMessage(Buffer::Instance& buffer, uint32_t remain_len) {
  int origin_len = buffer.length();
  // OldAuthSwitchRequest
  if (BufferHelper::endOfBuffer(buffer)) {
    setIsOldAuthSwitch(true);
    return DecodeStatus::Success;
  }
  if (BufferHelper::readString(buffer, auth_plugin_name_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing auth plugin name mysql Login response msg");
    return DecodeStatus::Failure;
  }
  int consumed_len = origin_len - buffer.length();
  if (BufferHelper::readStringBySize(buffer, remain_len - consumed_len, auth_plugin_data_) !=
      DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing auth plugin data code in mysql Login Ok msg");
    return DecodeStatus::Failure;
  }
  setIsOldAuthSwitch(false);
  return DecodeStatus::Success;
}

// https://dev.mysql.com/doc/internals/en/packet-OK_Packet.html
DecodeStatus OkMessage::parseMessage(Buffer::Instance& buffer, uint32_t len) {
  uint32_t init_len = buffer.length();
  ASSERT(init_len >= len);
  if (BufferHelper::readLengthEncodedInteger(buffer, affected_rows_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing affected_rows in mysql Login Ok msg");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readLengthEncodedInteger(buffer, last_insert_id_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing last_insert_id in mysql Login Ok msg");
    return DecodeStatus::Failure;
  }

  if (BufferHelper::readUint16(buffer, status_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing status in mysql Login Ok msg");
    return DecodeStatus::Failure;
  }
  // the exist of warning field is determined by server cap flag, but a decoder can not know the
  // cap flag, so just assume the CLIENT_PROTOCOL_41 is always set. ref
  // https://github.com/mysql/mysql-connector-j/blob/release/8.0/src/main/protocol-impl/java/com/mysql/cj/protocol/a/result/OkPacket.java#L48
  if (BufferHelper::readUint16(buffer, warnings_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing warnings in mysql Login Ok msg");
    return DecodeStatus::Failure;
  }
  uint32_t consumed_len = init_len - buffer.length();
  // The tail might contain info base on cap flag and status flag, but just consume all
  if (BufferHelper::readStringBySize(buffer, len - consumed_len, info_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing info in mysql Login Ok msg");
    return DecodeStatus::Failure;
  }
  return DecodeStatus::Success;
}

// https://dev.mysql.com/doc/internals/en/packet-ERR_Packet.html
DecodeStatus ErrMessage::parseMessage(Buffer::Instance& buffer, uint32_t remain_len) {
  int origin_len = buffer.length();
  if (BufferHelper::readUint16(buffer, error_code_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing error code in mysql Login error msg");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readUint8(buffer, marker_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing sql state marker in mysql Login error msg");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readStringBySize(buffer, MYSQL_SQL_STATE_LEN, sql_state_) !=
      DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing sql state in mysql Login error msg");
    return DecodeStatus::Failure;
  }
  int consumed_len = origin_len - buffer.length();
  if (BufferHelper::readStringBySize(buffer, remain_len - consumed_len, error_message_) !=
      DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing error message in mysql Login error msg");
    return DecodeStatus::Failure;
  }
  return DecodeStatus::Success;
}

// https://dev.mysql.com/doc/internals/en/connection-phase-packets.html#packet-Protocol::AuthMoreData
DecodeStatus AuthMoreMessage::parseMessage(Buffer::Instance& buffer, uint32_t remain_len) {
  if (BufferHelper::readStringBySize(buffer, remain_len, more_plugin_data_) !=
      DecodeStatus::Success) {
    ENVOY_LOG(info, "error parsing more plugin data in mysql Login auth more msg");
    return DecodeStatus::Failure;
  }
  return DecodeStatus::Success;
}

void ClientLoginResponse::encode(Buffer::Instance& out) {
  if (message_ == nullptr) {
    return;
  }
  message_->encode(out);
}

void AuthSwitchMessage::encode(Buffer::Instance& out) {
  BufferHelper::addUint8(out, MYSQL_RESP_AUTH_SWITCH);
  if (isOldAuthSwitch()) {
    return;
  }
  BufferHelper::addString(out, auth_plugin_name_);
  BufferHelper::addUint8(out, 0);
  BufferHelper::addString(out, auth_plugin_data_);
}

void OkMessage::encode(Buffer::Instance& out) {
  BufferHelper::addUint8(out, MYSQL_RESP_OK);
  BufferHelper::addLengthEncodedInteger(out, affected_rows_);
  BufferHelper::addLengthEncodedInteger(out, last_insert_id_);
  BufferHelper::addUint16(out, status_);
  BufferHelper::addUint16(out, warnings_);
  BufferHelper::addString(out, info_);
}

void ErrMessage::encode(Buffer::Instance& out) {
  BufferHelper::addUint8(out, MYSQL_RESP_ERR);
  BufferHelper::addUint16(out, error_code_);
  BufferHelper::addUint8(out, marker_);
  BufferHelper::addString(out, sql_state_);
  BufferHelper::addString(out, error_message_);
}

void AuthMoreMessage::encode(Buffer::Instance& out) {
  BufferHelper::addUint8(out, MYSQL_RESP_MORE);
  BufferHelper::addString(out, more_plugin_data_);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
