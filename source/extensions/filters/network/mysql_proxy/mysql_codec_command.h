#pragma once
#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class Command : public MySQLCodec {
public:
  // MySQLCodec
  // TODO(rshriram): plug in sql parser
  int Decode(Buffer::Instance&) override;
  std::string Encode() override;

  MySQLCodec::Cmd GetCmd() const { return cmd_; }
  const std::string& GetData() const { return data_; }
  void SetCmd(MySQLCodec::Cmd cmd);
  void SetData(std::string& data);

private:
  MySQLCodec::Cmd cmd_;
  std::string data_;
};

class CommandResp : public MySQLCodec {
public:
  // MySQLCodec
  // TODO(rshriram): plug in sql parser
  int Decode(Buffer::Instance&) override { return 0; }
  std::string Encode() override { return ""; }

  uint16_t GetServerStatus() const { return server_status_; }
  uint16_t GetWarnings() const { return warnings_; }
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
