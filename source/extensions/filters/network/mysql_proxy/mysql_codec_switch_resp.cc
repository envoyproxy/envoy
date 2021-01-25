#include "extensions/filters/network/mysql_proxy/mysql_codec_switch_resp.h"

#include "envoy/buffer/buffer.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

void ClientSwitchResponse::setAuthPluginResp(const std::string& auth_plugin_resp) {
  auth_plugin_resp_.assign(auth_plugin_resp);
}

int ClientSwitchResponse::parseMessage(Buffer::Instance&, uint32_t) { return MYSQL_SUCCESS; }

void ClientSwitchResponse::encode(Buffer::Instance& out) {
  BufferHelper::addString(out, auth_plugin_resp_);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
