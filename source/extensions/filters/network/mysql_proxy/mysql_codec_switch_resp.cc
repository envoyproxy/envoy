#include "extensions/filters/network/mysql_proxy/mysql_codec_switch_resp.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

void ClientSwitchResponse::setAuthPluginResp(std::string& auth_plugin_resp) {
  auth_plugin_resp_.assign(auth_plugin_resp);
}

int ClientSwitchResponse::parseMessage(Buffer::Instance&, uint32_t) { return MYSQL_SUCCESS; }

std::string ClientSwitchResponse::encode() {
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());

  BufferHelper::addString(*buffer, auth_plugin_resp_);
  return buffer->toString();
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
