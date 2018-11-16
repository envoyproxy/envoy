#pragma once
#include <cstdint>

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class MySQLCodecSrvResp : public MySQLCodec {
private:
  uint16_t server_status_;
  uint16_t warnings_;

public:
  MySQLCodecSrvResp();
  int Decode(Buffer::Instance& buffer);
  std::string& Encode();
  uint16_t GetServerStatus() { return server_status_; }
  uint16_t GetWarnings() { return warnings_; }
  void SetServerStatus(uint16_t status);
  void SetWarnings(uint16_t warnings);
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
