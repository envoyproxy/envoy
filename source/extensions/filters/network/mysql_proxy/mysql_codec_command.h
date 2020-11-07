#pragma once
#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class Command : public MySQLCodec {
public:
  enum class Cmd {
    Null = -1,
    Sleep = 0,
    Quit = 1,
    InitDb = 2,
    Query = 3,
    FieldList = 4,
    CreateDb = 5,
    DropDb = 6,
    Refresh = 7,
    Shutdown = 8,
    Statistics = 9,
    ProcessInfo = 10,
    Connect = 11,
    ProcessKill = 12,
    Debug = 13,
    Ping = 14,
    Time = 15,
    DelayedInsert = 16,
    ChangeUser = 17,
    Daemon = 29,
    ResetConnection = 31,
  };

  // MySQLCodec
  int parseMessage(Buffer::Instance&, uint32_t len) override;
  std::string encode() override;

  Cmd parseCmd(Buffer::Instance& data);
  Cmd getCmd() const { return cmd_; }
  const std::string& getData() const { return data_; }
  std::string& getDb() { return db_; }
  void setCmd(Cmd cmd);
  void setData(std::string& data);
  void setDb(std::string db);
  bool isQuery() { return is_query_; }

private:
  Cmd cmd_;
  std::string data_;
  std::string db_;
  bool is_query_;
};

class CommandResponse : public MySQLCodec {
public:
  // MySQLCodec
  int parseMessage(Buffer::Instance&, uint32_t) override { return MYSQL_SUCCESS; }
  std::string encode() override { return ""; }

  void setServerStatus(uint16_t status);
  void setWarnings(uint16_t warnings);

private:
  uint16_t server_status_;
  uint16_t warnings_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
