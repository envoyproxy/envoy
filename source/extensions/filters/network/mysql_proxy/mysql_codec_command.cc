#include "extensions/filters/network/mysql_proxy/mysql_codec_command.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

void Command::SetCmd(MySQLCodec::Cmd cmd) { cmd_ = cmd; }

int Command::Decode(Buffer::Instance& buffer) {
  int len = 0;
  int seq = 0;

  if (HdrReadDrain(buffer, len, seq) != MYSQL_SUCCESS) {
    ENVOY_LOG(error, "error parsing mysql HDR in mysql Command msg");
    return MYSQL_FAILURE;
  }
  SetSeq(seq);

  MySQLCodec::Cmd cmd = ParseCmd(buffer);
  SetCmd(cmd);
  if (cmd == MySQLCodec::Cmd::COM_NULL) {
    return MYSQL_FAILURE;
  }

  return MYSQL_SUCCESS;
}

void Command::SetData(std::string& data) { data_.assign(data); }

std::string Command::Encode() {
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());

  BufUint8Add(*buffer, static_cast<int>(cmd_));
  BufStringAdd(*buffer, data_);
  std::string e_string = BufToString(*buffer);
  return e_string;
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
