#include "extensions/filters/network/mysql_proxy/mysql_codec_command.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

Cmd Command::ParseCmd(Buffer::Instance& data) {
  uint8_t cmd;
  if (BufferHelper::BufUint8Drain(data, cmd) != MYSQL_SUCCESS) {
    return Cmd::COM_NULL;
  }
  return static_cast<Cmd>(cmd);
}

void Command::SetCmd(Cmd cmd) { cmd_ = cmd; }

void Command::SetDb(std::string db) { db_ = db; }

int Command::Decode(Buffer::Instance& buffer) {
  int len = 0;
  int seq = 0;

  if (BufferHelper::HdrReadDrain(buffer, len, seq) != MYSQL_SUCCESS) {
    ENVOY_LOG(error, "error parsing mysql HDR in mysql Command msg");
    return MYSQL_FAILURE;
  }
  SetSeq(seq);

  Cmd cmd = ParseCmd(buffer);
  SetCmd(cmd);
  if (cmd == Cmd::COM_NULL) {
    return MYSQL_FAILURE;
  }

  switch (cmd) {
  case Cmd::COM_INIT_DB:
  case Cmd::COM_CREATE_DB:
  case Cmd::COM_DROP_DB: {
    std::string db = "";
    BufferHelper::BufStringDrainBySize(buffer, db, len - 1);
    SetDb(db);
    break;
  }

  case Cmd::COM_QUERY:
    run_query_parser_ = true;
    // query string starts after mysql_hdr + one byte for comm type
    BufferHelper::BufStringDrainBySize(buffer, data_,
                                       buffer.length() - (sizeof(uint8_t) + MYSQL_HDR_SIZE));
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

  BufferHelper::BufUint8Add(*buffer, static_cast<int>(cmd_));
  BufferHelper::BufStringAdd(*buffer, data_);
  std::string e_string = BufferHelper::BufToString(*buffer);
  return e_string;
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
