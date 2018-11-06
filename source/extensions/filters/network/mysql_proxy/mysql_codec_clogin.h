#pragma once
#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MysqlProxy {

class ClientLogin : public MysqlCodec {
private:
#define UNSET_BYTES 23

  int client_cap_;
  int extended_client_cap_;
  int max_packet_;
  int charset_;
  std::string username_;
  std::string auth_resp_;
  std::string db_;

public:
  int Decode(Buffer::Instance& buffer);
  std::string Encode();
  int GetClientCap() { return client_cap_; }
  int GetExtendedClientCap() { return extended_client_cap_; }
  int GetMaxPacket() { return max_packet_; }
  int GetCharset() { return charset_; }
  std::string& GetUsername() { return username_; }
  std::string& GetAuthResp() { return auth_resp_; }
  std::string& GetDB() { return db_; }
  bool IsResponse41();
  bool IsResponse320();
  bool IsSSLRequest();
  bool IsConnectWithDb();
  bool IsClientAuthLenClData();
  bool IsClientSecureConnection();
  void SetClientCap(int client_cap);
  void SetExtendedClientCap(int extended_client_cap);
  void SetMaxPacket(int max_packet);
  void SetCharset(int charset);
  void SetUsername(std::string& username);
  void SetAuthResp(std::string& auth_resp);
  void SetDB(std::string& db);
};

} // namespace MysqlProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
