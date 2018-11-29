#include "extensions/filters/network/mysql_proxy/mysql_codec_switch_resp.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

void ClientSwitchResponse::SetSeq(int seq) { seq_ = seq; }

void ClientSwitchResponse::SetAuthPluginResp(std::string& auth_plugin_resp_) {
  auth_plugin_resp_.assign(auth_plugin_resp_);
}

int ClientSwitchResponse::Decode(Buffer::Instance&, uint64_t&, int seq, int) {
  SetSeq(seq);
  return MYSQL_SUCCESS;
}

std::string ClientSwitchResponse::Encode() {
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());

  BufferHelper::addString(*buffer, auth_plugin_resp_);
  std::string e_string = BufferHelper::toString(*buffer);
  return e_string;
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
