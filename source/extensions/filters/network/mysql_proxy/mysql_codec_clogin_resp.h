#pragma once
#include <cstdint>

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class ClientLoginResponse : public MySQLCodec {
public:
  // MySQLCodec
  int Decode(Buffer::Instance& buffer) override;
  std::string Encode() override;

  uint8_t GetRespCode() { return resp_code_; }
  uint8_t GetAffectedRows() { return affected_rows_; }
  uint8_t GetLastInsertId() { return last_insert_id_; }
  uint16_t GetServerStatus() { return server_status_; }
  uint16_t GetWarnings() { return warnings_; }
  void SetRespCode(uint8_t resp_code);
  void SetAffectedRows(uint8_t affected_rows);
  void SetLastInsertId(uint8_t last_insert_id);
  void SetServerStatus(uint16_t status);
  void SetWarnings(uint16_t warnings);

private:
  uint8_t resp_code_;
  uint8_t affected_rows_;
  uint8_t last_insert_id_;
  uint16_t server_status_;
  uint16_t warnings_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
