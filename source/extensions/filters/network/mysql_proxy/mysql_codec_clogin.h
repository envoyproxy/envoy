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
  int Decode(Buffer::Instance& buffer) override;
  std::string Encode() override;

  int GetClientCap() const { return client_cap_; }
  int GetExtendedClientCap() const { return extended_client_cap_; }
  int GetMaxPacket() const { return max_packet_; }
  int GetCharset() const { return charset_; }
  const std::string& GetUsername() const { return username_; }
  const std::string& GetAuthResp() const { return auth_resp_; }
  const std::string& GetDB() const { return db_; }
  bool IsResponse41() const;
  bool IsResponse320() const;
  bool IsSSLRequest() const;
  bool IsConnectWithDb() const;
  bool IsClientAuthLenClData() const;
  bool IsClientSecureConnection() const;
  void SetClientCap(int client_cap);
  void SetExtendedClientCap(int extended_client_cap);
  void SetMaxPacket(int max_packet);
  void SetCharset(int charset);
  void SetUsername(std::string& username);
  void SetAuthResp(std::string& auth_resp);
  void SetDB(std::string& db);

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
