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
  int Decode(Buffer::Instance&, uint64_t& offset, int seq, int len) override;
  std::string Encode() override;

  Cmd ParseCmd(Buffer::Instance& data, uint64_t& offset);
  Cmd GetCmd() const { return cmd_; }
  const std::string& GetData() const { return data_; }
  std::string& GetDb() { return db_; }
  void SetCmd(Cmd cmd);
  void SetData(std::string& data);
  void SetDb(std::string db);
  bool RunQueryParser() { return run_query_parser_; }

private:
  Cmd cmd_;
  std::string data_;
  std::string db_;
  bool run_query_parser_;
};

class CommandResp : public MySQLCodec {
public:
  // MySQLCodec
  int Decode(Buffer::Instance&, uint64_t&, int, int) override { return 0; }
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
