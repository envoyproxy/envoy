#include "contrib/mysql_proxy/filters/network/source/mysql_codec_clogin_resp.h"

#include "envoy/buffer/buffer.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

#include "contrib/mysql_proxy/filters/network/source/mysql_codec.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

// https://dev.mysql.com/doc/internals/en/connection-phase-packets.html#packet-Protocol::AuthSwitchRequest
DecodeStatus AuthSwitchMessage::parseMessage(Buffer::Instance& buffer, uint32_t remain_len) {
  int origin_len = buffer.length();
  if (BufferHelper::readUint8(buffer, resp_code_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error parsing resp code of auth switch msg");
    return DecodeStatus::Failure;
  }
  ASSERT(resp_code_ == MYSQL_RESP_AUTH_SWITCH);
  // OldAuthSwitchRequest
  if (BufferHelper::endOfBuffer(buffer)) {
    setIsOldAuthSwitch(true);
    return DecodeStatus::Success;
  }
  if (BufferHelper::readString(buffer, auth_plugin_name_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error parsing auth plugin name mysql Login response msg");
    return DecodeStatus::Failure;
  }
  int consumed_len = origin_len - buffer.length();
  if (BufferHelper::readVectorBySize(buffer, remain_len - consumed_len, auth_plugin_data_) !=
      DecodeStatus::Success) {
    ENVOY_LOG(debug, "error parsing auth plugin data code of auth switch msg");

    return DecodeStatus::Failure;
  }
  setIsOldAuthSwitch(false);
  return DecodeStatus::Success;
}

// https://dev.mysql.com/doc/internals/en/packet-OK_Packet.html
DecodeStatus OkMessage::parseMessage(Buffer::Instance& buffer, uint32_t len) {
  uint32_t init_len = buffer.length();
  if (BufferHelper::readUint8(buffer, resp_code_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error parsing resp code of ok msg");
    return DecodeStatus::Failure;
  }
  ASSERT(resp_code_ == MYSQL_RESP_OK);
  if (BufferHelper::readLengthEncodedInteger(buffer, affected_rows_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error parsing affected_rows of  ok msg");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readLengthEncodedInteger(buffer, last_insert_id_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error parsing last_insert_id of ok msg");
    return DecodeStatus::Failure;
  }

  if (BufferHelper::readUint16(buffer, status_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error parsing status of ok msg");
    return DecodeStatus::Failure;
  }
  // the exist of warning field is determined by server cap flag, but a decoder can not know the
  // cap flag, so just assume the CLIENT_PROTOCOL_41 is always set. ref
  // https://github.com/mysql/mysql-connector-j/blob/release/8.0/src/main/protocol-impl/java/com/mysql/cj/protocol/a/result/OkPacket.java#L48
  if (BufferHelper::readUint16(buffer, warnings_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error parsing warnings of ok msg");
    return DecodeStatus::Failure;
  }
  uint32_t consumed_len = init_len - buffer.length();
  // The tail might contain debug base on cap flag and status flag, but just consume all
  if (BufferHelper::readStringBySize(buffer, len - consumed_len, info_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error parsing debug of ok msg");
    return DecodeStatus::Failure;
  }
  return DecodeStatus::Success;
}

// https://dev.mysql.com/doc/internals/en/packet-ERR_Packet.html
DecodeStatus ErrMessage::parseMessage(Buffer::Instance& buffer, uint32_t remain_len) {
  int origin_len = buffer.length();
  if (BufferHelper::readUint8(buffer, resp_code_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error parsing resp code of error msg");
    return DecodeStatus::Failure;
  }
  ASSERT(resp_code_ == MYSQL_RESP_ERR);
  if (BufferHelper::readUint16(buffer, error_code_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error parsing error code of error msg");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readUint8(buffer, marker_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error parsing sql state marker of error msg");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readStringBySize(buffer, MYSQL_SQL_STATE_LEN, sql_state_) !=
      DecodeStatus::Success) {
    ENVOY_LOG(debug, "error parsing sql state of error msg");
    return DecodeStatus::Failure;
  }
  int consumed_len = origin_len - buffer.length();
  if (BufferHelper::readStringBySize(buffer, remain_len - consumed_len, error_message_) !=
      DecodeStatus::Success) {
    ENVOY_LOG(debug, "error parsing error message of error msg");
    return DecodeStatus::Failure;
  }
  return DecodeStatus::Success;
}

// https://dev.mysql.com/doc/internals/en/connection-phase-packets.html#packet-Protocol::AuthMoreData
DecodeStatus AuthMoreMessage::parseMessage(Buffer::Instance& buffer, uint32_t remain_len) {
  if (BufferHelper::readUint8(buffer, resp_code_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error parsing resp code of auth more msg");
    return DecodeStatus::Failure;
  }
  ASSERT(resp_code_ == MYSQL_RESP_MORE);
  if (BufferHelper::readVectorBySize(buffer, remain_len - sizeof(uint8_t), more_plugin_data_) !=
      DecodeStatus::Success) {
    ENVOY_LOG(debug, "error parsing more plugin data of auth more msg");
    return DecodeStatus::Failure;
  }
  return DecodeStatus::Success;
}

void AuthSwitchMessage::encode(Buffer::Instance& out) const {
  BufferHelper::addUint8(out, resp_code_);
  if (isOldAuthSwitch()) {
    return;
  }
  BufferHelper::addString(out, auth_plugin_name_);
  BufferHelper::addUint8(out, 0);
  BufferHelper::addVector(out, auth_plugin_data_);
}

void OkMessage::encode(Buffer::Instance& out) const {
  BufferHelper::addUint8(out, resp_code_);
  BufferHelper::addLengthEncodedInteger(out, affected_rows_);
  BufferHelper::addLengthEncodedInteger(out, last_insert_id_);
  BufferHelper::addUint16(out, status_);
  BufferHelper::addUint16(out, warnings_);
  BufferHelper::addString(out, info_);
}

void ErrMessage::encode(Buffer::Instance& out) const {
  BufferHelper::addUint8(out, resp_code_);
  BufferHelper::addUint16(out, error_code_);
  BufferHelper::addUint8(out, marker_);
  BufferHelper::addString(out, sql_state_);
  BufferHelper::addString(out, error_message_);
}

void AuthMoreMessage::encode(Buffer::Instance& out) const {
  BufferHelper::addUint8(out, resp_code_);
  BufferHelper::addVector(out, more_plugin_data_);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
