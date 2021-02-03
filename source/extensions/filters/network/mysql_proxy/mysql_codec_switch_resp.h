#pragma once

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class ClientSwitchResponse : public MySQLCodec {
public:
  // MySQLCodec
  DecodeStatus parseMessage(Buffer::Instance& buffer, uint32_t len) override;
  void encode(Buffer::Instance&) override;

  void setAuthPluginResp(const std::string& auth_plugin_resp) {
    auth_plugin_resp_ = auth_plugin_resp;
  }
  const std::string& getAuthPluginResp() const { return auth_plugin_resp_; }

private:
  std::string auth_plugin_resp_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
