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
  int parseMessage(Buffer::Instance& buffer, uint32_t len) override;
  std::string encode() override;

  uint8_t getRespCode() const { return resp_code_; }
  uint8_t getAffectedRows() const { return affected_rows_; }
  uint8_t getLastInsertId() const { return last_insert_id_; }
  uint16_t getServerStatus() const { return server_status_; }
  uint16_t getWarnings() const { return warnings_; }
  void setRespCode(uint8_t resp_code);
  void setAffectedRows(uint8_t affected_rows);
  void setLastInsertId(uint8_t last_insert_id);
  void setServerStatus(uint16_t status);
  void setWarnings(uint16_t warnings);

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
