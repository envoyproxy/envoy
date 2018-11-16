#pragma once
#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class ClientSwitchResponse : public MySQLCodec {
private:
  std::string auth_plugin_resp_;
  int seq_;

public:
  int GetSeq() { return seq_; }
  void SetSeq(int seq);
  std::string& GetAuthPluginResp() { return auth_plugin_resp_; }
  void SetAuthPluginResp(std::string& auth_swith_resp);
  int Decode(Buffer::Instance& buffer);
  std::string Encode();
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
