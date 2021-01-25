#pragma once
#include "common/buffer/buffer_impl.h"

#include "envoy/buffer/buffer.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class ClientSwitchResponse : public MySQLCodec {
public:
  // MySQLCodec
  int parseMessage(Buffer::Instance& buffer, uint32_t len) override;
  void encode(Buffer::Instance&) override;

  void setAuthPluginResp(const std::string& auth_swith_resp);

private:
  std::string auth_plugin_resp_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
