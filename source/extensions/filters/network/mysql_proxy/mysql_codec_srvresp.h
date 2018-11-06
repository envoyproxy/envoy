#pragma once
#include "common/buffer/buffer_impl.h"

namespace Envoy {
    namespace Extensions {
namespace NetworkFilters {
namespace MysqlProxy {

class MysqlCodecSrvResp : public MysqlCodec {
private:
  uint16_t server_status_;
  uint16_t warnings_;

public:
  MysqlCodecSrvResp();
  int Decode(Buffer::Instance& buffer);
  std::string& Encode();
  uint16_t GetServerStatus() { return server_status_; }
  uint16_t GetWarnings() { return warnings_; }
  void SetServerStatus(uint16_t status);
  void SetWarnings(uint16_t warnings);
};

} // namespace MysqlProxy
} // namespace NetworkFilters
} // namespace Extensions
    } // namespace Envoy
