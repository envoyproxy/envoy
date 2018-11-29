#include "extensions/filters/network/mysql_proxy/mysql_codec_switch_resp.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

void ClientSwitchResponse::setAuthPluginResp(std::string& auth_plugin_resp_) {
  auth_plugin_resp_.assign(auth_plugin_resp_);
}

int ClientSwitchResponse::decode(Buffer::Instance&, uint64_t&, int seq, int) {
  setSeq(seq);
  return MYSQL_SUCCESS;
}

std::string ClientSwitchResponse::encode() {
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());

  BufferHelper::addString(*buffer, auth_plugin_resp_);
  std::string e_string = BufferHelper::toString(*buffer);
  return e_string;
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
