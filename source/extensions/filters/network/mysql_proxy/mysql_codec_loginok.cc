#include "mysql_codec_loginok.h"
#include "mysql_codec.h"

namespace Envoy {
  namespace Extensions {
namespace NetworkFilters {
namespace MysqlProxy {

void ClientLoginResponse::SetRespCode(uint8_t resp_code) { resp_code_ = resp_code; }

void ClientLoginResponse::SetAffectedRows(uint8_t affected_rows) { affected_rows_ = affected_rows; }

void ClientLoginResponse::SetLastInsertId(uint8_t last_insert_id) {
  last_insert_id_ = last_insert_id;
}

void ClientLoginResponse::SetServerStatus(uint16_t status) { server_status_ = status; }

void ClientLoginResponse::SetWarnings(uint16_t warnings) { warnings_ = warnings; }

int ClientLoginResponse::Decode(Buffer::Instance& buffer) {
  int len = 0;
  int seq = 0;
  if (HdrReadDrain(buffer, len, seq) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing mysql HDR in mysql ClientLogin msg");
    return MYSQL_FAILURE;
  }
  SetSeq(seq);
  if (seq != CHALLENGE_RESP_SEQ_NUM) {
    return MYSQL_FAILURE;
  }
  uint8_t resp_code = 0;
  if (BufUint8Drain(buffer, resp_code) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing response code in mysql Login Ok msg");
    return MYSQL_FAILURE;
  }
  SetRespCode(resp_code);
  if ((resp_code == MYSQL_RESP_AUTH_SWITCH) && EndOfBuffer(buffer)) {
    /* OldAuthSwitchRequest */
    return MYSQL_SUCCESS;
  }
  uint8_t affected_rows = 0;
  if (BufUint8Drain(buffer, affected_rows) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing affected_rows in mysql Login Ok msg");
    return MYSQL_FAILURE;
  }
  SetAffectedRows(affected_rows);
  uint8_t last_insert_id = 0;
  if (BufUint8Drain(buffer, last_insert_id) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing last_insert_id in mysql Login Ok msg");
    return MYSQL_FAILURE;
  }
  SetLastInsertId(last_insert_id);
  uint16_t server_status = 0;
  if (BufUint16Drain(buffer, server_status) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing server_status in mysql Login Ok msg");
    return MYSQL_FAILURE;
  }
  SetServerStatus(server_status);
  uint16_t warnings = 0;
  if (BufUint16Drain(buffer, warnings) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing warnings in mysql Login Ok msg");
    return MYSQL_FAILURE;
  }
  SetWarnings(warnings);
  return MYSQL_SUCCESS;
}

std::string ClientLoginResponse::Encode() {
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  BufUint8Add(*buffer, resp_code_);
  BufUint8Add(*buffer, affected_rows_);
  BufUint8Add(*buffer, last_insert_id_);
  BufUint16Add(*buffer, server_status_);
  BufUint16Add(*buffer, warnings_);

  std::string e_string = BufToString(*buffer);
  return e_string;
}

} // namespace MysqlProxy
} // namespace NetworkFilters
} // namespace Extensions
  } // namespace Envoy
