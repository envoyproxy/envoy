#pragma once
#include "envoy/buffer/buffer.h"

#include "source/common/buffer/buffer_impl.h"

#include "contrib/mysql_proxy/filters/network/source/mysql_codec.h"

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
  DecodeStatus parseMessage(Buffer::Instance&, uint32_t len) override;
  void encode(Buffer::Instance&) const override;

  Cmd parseCmd(Buffer::Instance& data);
  Cmd getCmd() const { return cmd_; }
  const std::string& getData() const { return data_; }
  std::string& getDb() { return db_; }
  void setCmd(Cmd cmd);
  void setData(const std::string& data);
  void setDb(const std::string& db);
  void setIsQuery(bool is_query) { is_query_ = is_query; }
  bool isQuery() { return is_query_; }

private:
  Cmd cmd_;
  std::string data_;
  std::string db_;
  bool is_query_;
};

/* CommandResponse has many types. ref https://dev.mysql.com/doc/internals/en/text-protocol.html
 * We just get all data
 */
class CommandResponse : public MySQLCodec {
public:
  // MySQLCodec
  DecodeStatus parseMessage(Buffer::Instance&, uint32_t) override;
  void encode(Buffer::Instance&) const override;
  const std::string& getData() const { return data_; }
  void setData(const std::string& data) { data_ = data; }

private:
  std::string data_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
