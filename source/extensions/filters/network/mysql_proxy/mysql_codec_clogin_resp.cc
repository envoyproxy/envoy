#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

void ClientLoginResponse::setRespCode(uint8_t resp_code) { resp_code_ = resp_code; }

void ClientLoginResponse::setAffectedRows(uint8_t affected_rows) { affected_rows_ = affected_rows; }

void ClientLoginResponse::setLastInsertId(uint8_t last_insert_id) {
  last_insert_id_ = last_insert_id;
}

void ClientLoginResponse::setServerStatus(uint16_t status) { server_status_ = status; }

void ClientLoginResponse::setWarnings(uint16_t warnings) { warnings_ = warnings; }

int ClientLoginResponse::parseMessage(Buffer::Instance& buffer, uint32_t) {
  uint8_t resp_code = 0;
  if (BufferHelper::readUint8(buffer, resp_code) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing response code in mysql Login Ok msg");
    return MYSQL_FAILURE;
  }
  setRespCode(resp_code);
  if ((resp_code == MYSQL_RESP_AUTH_SWITCH) && BufferHelper::endOfBuffer(buffer)) {
    // OldAuthSwitchRequest
    return MYSQL_SUCCESS;
  }
  uint8_t affected_rows = 0;
  if (BufferHelper::readUint8(buffer, affected_rows) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing affected_rows in mysql Login Ok msg");
    return MYSQL_FAILURE;
  }
  setAffectedRows(affected_rows);
  uint8_t last_insert_id = 0;
  if (BufferHelper::readUint8(buffer, last_insert_id) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing last_insert_id in mysql Login Ok msg");
    return MYSQL_FAILURE;
  }
  setLastInsertId(last_insert_id);
  uint16_t server_status = 0;
  if (BufferHelper::readUint16(buffer, server_status) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing server_status in mysql Login Ok msg");
    return MYSQL_FAILURE;
  }
  setServerStatus(server_status);
  uint16_t warnings = 0;
  if (BufferHelper::readUint16(buffer, warnings) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing warnings in mysql Login Ok msg");
    return MYSQL_FAILURE;
  }
  setWarnings(warnings);
  return MYSQL_SUCCESS;
}

std::string ClientLoginResponse::encode() {
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  BufferHelper::addUint8(*buffer, resp_code_);
  BufferHelper::addUint8(*buffer, affected_rows_);
  BufferHelper::addUint8(*buffer, last_insert_id_);
  BufferHelper::addUint16(*buffer, server_status_);
  BufferHelper::addUint16(*buffer, warnings_);

  std::string e_string = buffer->toString();
  return e_string;
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
