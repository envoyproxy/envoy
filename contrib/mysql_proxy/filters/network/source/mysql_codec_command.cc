#include "contrib/mysql_proxy/filters/network/source/mysql_codec_command.h"

#include "envoy/buffer/buffer.h"

#include "source/common/common/logger.h"
#include "source/common/common/macros.h"

#include "contrib/mysql_proxy/filters/network/source/mysql_codec.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

Command::Cmd Command::parseCmd(Buffer::Instance& data) {
  uint8_t cmd;
  if (BufferHelper::readUint8(data, cmd) != DecodeStatus::Success) {
    return Command::Cmd::Null;
  }
  return static_cast<Command::Cmd>(cmd);
}

void Command::setCmd(Command::Cmd cmd) { cmd_ = cmd; }

void Command::setDb(const std::string& db) { db_ = db; }

DecodeStatus Command::parseMessage(Buffer::Instance& buffer, uint32_t len) {
  Command::Cmd cmd = parseCmd(buffer);
  setCmd(cmd);
  if (cmd == Command::Cmd::Null) {
    return DecodeStatus::Failure;
  }

  switch (cmd) {
  case Command::Cmd::InitDb:
  case Command::Cmd::CreateDb:
  case Command::Cmd::DropDb: {
    std::string db;
    BufferHelper::readStringBySize(buffer, len - 1, db);
    setDb(db);
    break;
  }
  case Command::Cmd::Query:
    is_query_ = true;
    FALLTHRU;
  default:
    BufferHelper::readStringBySize(buffer, len - 1, data_);
    break;
  }
  return DecodeStatus::Success;
}

void Command::setData(const std::string& data) { data_.assign(data); }

void Command::encode(Buffer::Instance& out) const {
  BufferHelper::addUint8(out, static_cast<int>(cmd_));
  switch (cmd_) {
  case Command::Cmd::InitDb:
  case Command::Cmd::CreateDb:
  case Command::Cmd::DropDb: {
    BufferHelper::addString(out, db_);
    break;
  }
  default:
    BufferHelper::addString(out, data_);
    break;
  }
}

DecodeStatus CommandResponse::parseMessage(Buffer::Instance& buffer, uint32_t len) {
  if (BufferHelper::readStringBySize(buffer, len, data_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when parsing command response");
    return DecodeStatus::Failure;
  }
  return DecodeStatus::Success;
}

void CommandResponse::encode(Buffer::Instance& out) const { BufferHelper::addString(out, data_); }

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
