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
    COM_NULL = -1,
    COM_SLEEP = 0,
    COM_QUIT = 1,
    COM_INIT_DB = 2,
    COM_QUERY = 3,
    COM_FIELD_LIST = 4,
    COM_CREATE_DB = 5,
    COM_DROP_DB = 6,
    COM_REFRESH = 7,
    COM_SHUTDOWN = 8,
    COM_STATISTICS = 9,
    COM_PROCESS_INFO = 10,
    COM_CONNECT = 11,
    COM_PROCESS_KILL = 12,
    COM_DEBUG = 13,
    COM_PING = 14,
    COM_TIME = 15,
    COM_DELAYED_INSERT = 16,
    COM_CHANGE_USER = 17,
    COM_DAEMON = 29,
    COM_RESET_CONNECTION = 31,
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

  uint16_t getServerStatus() const { return server_status_; }
  uint16_t getWarnings() const { return warnings_; }
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
