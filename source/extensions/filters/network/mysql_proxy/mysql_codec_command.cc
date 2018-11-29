#include "extensions/filters/network/mysql_proxy/mysql_codec_command.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

Cmd Command::ParseCmd(Buffer::Instance& data, uint64_t& offset) {
  uint8_t cmd;
  if (BufferHelper::peekUint8(data, offset, cmd) != MYSQL_SUCCESS) {
    return Cmd::COM_NULL;
  }
  return static_cast<Cmd>(cmd);
}

void Command::SetCmd(Cmd cmd) { cmd_ = cmd; }

void Command::SetDb(std::string db) { db_ = db; }

int Command::Decode(Buffer::Instance& buffer, uint64_t& offset, int seq, int len) {
  SetSeq(seq);

  Cmd cmd = ParseCmd(buffer, offset);
  SetCmd(cmd);
  if (cmd == Cmd::COM_NULL) {
    return MYSQL_FAILURE;
  }

  switch (cmd) {
  case Cmd::COM_INIT_DB:
  case Cmd::COM_CREATE_DB:
  case Cmd::COM_DROP_DB: {
    std::string db = "";
    BufferHelper::peekStringBySize(buffer, offset, len - 1, db);
    SetDb(db);
    break;
  }

  case Cmd::COM_QUERY:
    run_query_parser_ = true;
    // query string starts after mysql_hdr + one byte for comm type
    BufferHelper::peekStringBySize(buffer, offset,
                                   buffer.length() - (sizeof(uint8_t) + MYSQL_HDR_SIZE), data_);
    SetDb("");
    break;

  default:
    SetDb("");
    break;
  }

  return MYSQL_SUCCESS;
}

void Command::SetData(std::string& data) { data_.assign(data); }

std::string Command::Encode() {
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());

  BufferHelper::addUint8(*buffer, static_cast<int>(cmd_));
  BufferHelper::addString(*buffer, data_);
  std::string e_string = BufferHelper::toString(*buffer);
  return e_string;
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
