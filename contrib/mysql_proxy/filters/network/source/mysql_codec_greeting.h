#pragma once
#include "source/common/buffer/buffer_impl.h"

#include "contrib/mysql_proxy/filters/network/source/mysql_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class ServerGreeting : public MySQLCodec {
public:
  // MySQLCodec
  DecodeStatus parseMessage(Buffer::Instance& buffer, uint32_t len) override;
  void encode(Buffer::Instance&) const override;

  uint8_t getProtocol() const { return protocol_; }
  const std::string& getVersion() const { return version_; }
  uint32_t getThreadId() const { return thread_id_; }
  const std::vector<uint8_t>& getAuthPluginData1() const { return auth_plugin_data1_; }
  const std::vector<uint8_t>& getAuthPluginData2() const { return auth_plugin_data2_; }
  std::vector<uint8_t> getAuthPluginData() const {
    if ((server_cap_ & CLIENT_PLUGIN_AUTH) || (server_cap_ & CLIENT_SECURE_CONNECTION)) {
      auto res = auth_plugin_data1_;
      res.insert(res.end(), auth_plugin_data2_.begin(), auth_plugin_data2_.end());
      return res;
    }
    return auth_plugin_data1_;
  }
  uint8_t getServerCharset() const { return server_charset_; }
  uint16_t getServerStatus() const { return server_status_; }
  uint32_t getServerCap() const { return server_cap_; }
  uint16_t getBaseServerCap() const { return server_cap_ & 0xffff; }
  uint16_t getExtServerCap() const { return server_cap_ >> 16; }
  const std::string& getAuthPluginName() const { return auth_plugin_name_; }

  void setProtocol(uint8_t protocol);
  void setVersion(const std::string& version);
  void setThreadId(uint32_t thread_id);
  void setServerCap(uint32_t server_cap);
  void setBaseServerCap(uint16_t base_server_cap);
  void setExtServerCap(uint16_t ext_server_cap);
  void setAuthPluginName(const std::string& name);
  void setAuthPluginData(const std::vector<uint8_t>& salt);
  void setAuthPluginData1(const std::vector<uint8_t>& salt);
  void setAuthPluginData2(const std::vector<uint8_t>& salt);
  void setServerCharset(uint8_t server_language);
  void setServerStatus(uint16_t server_status);

private:
  DecodeStatus check() const;

private:
  uint8_t protocol_{0};
  std::string version_;
  uint32_t thread_id_{0};
  std::vector<uint8_t> auth_plugin_data1_;
  std::vector<uint8_t> auth_plugin_data2_;
  uint32_t server_cap_{0};
  uint8_t server_charset_{0};
  uint16_t server_status_{0};
  std::string auth_plugin_name_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
