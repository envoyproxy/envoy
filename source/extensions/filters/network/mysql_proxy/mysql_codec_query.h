#pragma once
#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class MySQLCodecQuery : public MySQLCodec {
public:
  int Decode(Buffer::Instance& buffer);
  std::string Encode();
  MySQLCodec::Cmd GetCmd() { return cmd_; }
  std::string& GetOp() { return op_; }
  std::string& GetStatment() { return statement_; }
  void SetCmd(MySQLCodec::Cmd cmd);
  void SetOp(std::string& op);
  void SetStatement(std::string& statement);

private:
  MySQLCodec::Cmd cmd_;
  std::string op_;
  std::string statement_;
};

class MySQLCodecQueryResp : public MySQLCodec {
public:
  int Decode(Buffer::Instance& buffer);
  std::string& Encode();
  uint16_t GetServerStatus() { return server_status_; }
  uint16_t GetWarnings() { return warnings_; }
  void SetServerStatus(uint16_t status);
  void SetWarnings(uint16_t warnings);

private:
  uint16_t server_status_;
  uint16_t warnings_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
