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
  void encode(Buffer::Instance&) const override;

  void setAuthPluginResp(const std::vector<uint8_t>& auth_plugin_resp) {
    auth_plugin_resp_ = auth_plugin_resp;
  }
  const std::vector<uint8_t>& getAuthPluginResp() const { return auth_plugin_resp_; }

private:
  std::vector<uint8_t> auth_plugin_resp_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
