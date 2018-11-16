#pragma once
#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class ServerGreeting : public MySQLCodec {
private:
  int protocol_;
  std::string version_;
  int thread_id_;
  std::string salt_;
  int server_cap_;
  int server_language_;
  int server_status_;
  int ext_server_cap_;

public:
  int Decode(Buffer::Instance& buffer);
  std::string Encode();
  int GetProtocol() { return protocol_; }
  std::string& GetVersion() { return version_; }
  int GetThreadId() { return thread_id_; }
  std::string& GetSalt() { return salt_; };
  int GetServerCap() { return server_cap_; }
  int GetServerLanguage() { return server_language_; }
  int GetServerStatus() { return server_status_; }
  int GetExtServerCap() { return ext_server_cap_; }
  void SetProtocol(int protocol);
  void SetVersion(std::string& version);
  void SetThreadId(int thread_id);
  void SetSalt(std::string& salt);
  void SetServerCap(int server_cap);
  void SetServerLanguage(int server_language);
  void SetServerStatus(int server_status);
  void SetExtServerCap(int ext_server_cap);
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
