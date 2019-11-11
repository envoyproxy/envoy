#include "extensions/filters/network/mysql_proxy/mysql_codec_command.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

Command::Cmd Command::parseCmd(Buffer::Instance& data) {
  uint8_t cmd;
  if (BufferHelper::readUint8(data, cmd) != MYSQL_SUCCESS) {
    return Command::Cmd::Null;
  }
  return static_cast<Command::Cmd>(cmd);
}

void Command::setCmd(Command::Cmd cmd) { cmd_ = cmd; }

void Command::setDb(std::string db) { db_ = db; }

int Command::parseMessage(Buffer::Instance& buffer, uint32_t len) {
  Command::Cmd cmd = parseCmd(buffer);
  setCmd(cmd);
  if (cmd == Command::Cmd::Null) {
    return MYSQL_FAILURE;
  }

  switch (cmd) {
  case Command::Cmd::InitDb:
  case Command::Cmd::CreateDb:
  case Command::Cmd::DropDb: {
    std::string db = "";
    BufferHelper::readStringBySize(buffer, len - 1, db);
    setDb(db);
    break;
  }

  case Command::Cmd::Query:
    is_query_ = true;
    // query string starts after one byte for comm type
    BufferHelper::readStringBySize(buffer, len - 1, data_);
    setDb("");
    break;

  default:
    setDb("");
    break;
  }

  return MYSQL_SUCCESS;
}

void Command::setData(std::string& data) { data_.assign(data); }

std::string Command::encode() {
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());

  BufferHelper::addUint8(*buffer, static_cast<int>(cmd_));
  BufferHelper::addString(*buffer, data_);
  std::string e_string = buffer->toString();
  return e_string;
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
