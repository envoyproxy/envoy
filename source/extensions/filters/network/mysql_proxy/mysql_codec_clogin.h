#pragma once
#include "envoy/buffer/buffer.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

constexpr int UNSET_BYTES = 23;
class ClientLogin : public MySQLCodec {
public:
  // MySQLCodec
  int parseMessage(Buffer::Instance& buffer, uint32_t len) override;
  void encode(Buffer::Instance&) override;

  uint32_t getClientCap() const { return client_cap_; }
  uint16_t getBaseClientCap() const { return client_cap_ & 0xffff; }
  uint16_t getExtendedClientCap() const { return client_cap_ >> 16; }
  uint32_t getMaxPacket() const { return max_packet_; }
  uint8_t getCharset() const { return charset_; }
  const std::string& getUsername() const { return username_; }
  const std::string& getAuthResp() const { return auth_resp_; }
  const std::string& getDb() const { return db_; }
  const std::string& getAuthPluginName() const { return auth_plugin_name_; }
  bool isResponse41() const;
  bool isResponse320() const;
  bool isSSLRequest() const;
  bool isConnectWithDb() const;
  bool isClientAuthLenClData() const;
  bool isClientSecureConnection() const;
  void setClientCap(uint32_t client_cap);
  void setBaseClientCap(uint16_t base_cap);
  void setExtendedClientCap(uint16_t ext_cap);
  void setMaxPacket(uint32_t max_packet);
  void setCharset(uint8_t charset);
  void setUsername(const std::string& username);
  void setAuthResp(const std::string& auth_resp);
  void setDb(const std::string& db);
  void setAuthPluginName(const std::string& auth_plugin_name);

private:
  int parseResponseSsl(Buffer::Instance& buffer);
  int parseResponse41(Buffer::Instance& buffer);
  int parseResponse320(Buffer::Instance& buffer);
  void encodeResponseSsl(Buffer::Instance& out);
  void encodeResponse41(Buffer::Instance& out);
  void encodeResponse320(Buffer::Instance& out);

  uint32_t client_cap_;
  uint32_t max_packet_;
  uint8_t charset_;
  std::string username_;
  std::string auth_resp_;
  std::string db_;
  std::string auth_plugin_name_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
