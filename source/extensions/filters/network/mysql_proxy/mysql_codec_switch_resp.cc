#include "mysql_codec_switch_resp.h"
#include "mysql_codec.h"

namespace Envoy {
  namespace Extensions {
namespace NetworkFilters {
namespace MysqlProxy {

void ClientSwitchResponse::SetSeq(int seq) { seq_ = seq; }

void ClientSwitchResponse::SetAuthPluginResp(std::string& auth_plugin_resp_) {
  auth_plugin_resp_.assign(auth_plugin_resp_);
}

int ClientSwitchResponse::Decode(Buffer::Instance& buffer) {
  int len = 0;
  int seq = 0;
  if (HdrReadDrain(buffer, len, seq) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing mysql HDR in mysql ClientLogin msg");
    return MYSQL_FAILURE;
  }
  SetSeq(seq);
  return MYSQL_SUCCESS;
}

std::string ClientSwitchResponse::Encode() {
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());

  BufStringAdd(*buffer, auth_plugin_resp_);
  std::string e_string = BufToString(*buffer);
  return e_string;
}

} // namespace MysqlProxy
} // namespace NetworkFilters
} // namespace Extensions
  } // namespace Envoy
