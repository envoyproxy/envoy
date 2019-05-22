#pragma once
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
  std::string encode() override;

  int getClientCap() const { return client_cap_; }
  int getExtendedClientCap() const { return extended_client_cap_; }
  int getMaxPacket() const { return max_packet_; }
  int getCharset() const { return charset_; }
  const std::string& getUsername() const { return username_; }
  const std::string& getAuthResp() const { return auth_resp_; }
  const std::string& getDb() const { return db_; }
  bool isResponse41() const;
  bool isResponse320() const;
  bool isSSLRequest() const;
  bool isConnectWithDb() const;
  bool isClientAuthLenClData() const;
  bool isClientSecureConnection() const;
  void setClientCap(int client_cap);
  void setExtendedClientCap(int extended_client_cap);
  void setMaxPacket(int max_packet);
  void setCharset(int charset);
  void setUsername(std::string& username);
  void setAuthResp(std::string& auth_resp);
  void setDb(std::string& db);

private:
  int client_cap_;
  int extended_client_cap_;
  int max_packet_;
  int charset_;
  std::string username_;
  std::string auth_resp_;
  std::string db_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
