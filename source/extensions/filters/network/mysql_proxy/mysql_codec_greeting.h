#pragma once
#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class ServerGreeting : public MySQLCodec {
public:
  // MySQLCodec
  int Decode(Buffer::Instance& buffer) override;
  std::string Encode() override;

  int GetProtocol() const { return protocol_; }
  const std::string& GetVersion() const { return version_; }
  int GetThreadId() const { return thread_id_; }
  const std::string& GetSalt() const { return salt_; };
  int GetServerCap() const { return server_cap_; }
  int GetServerLanguage() const { return server_language_; }
  int GetServerStatus() const { return server_status_; }
  int GetExtServerCap() const { return ext_server_cap_; }
  void SetProtocol(int protocol);
  void SetVersion(std::string& version);
  void SetThreadId(int thread_id);
  void SetSalt(std::string& salt);
  void SetServerCap(int server_cap);
  void SetServerLanguage(int server_language);
  void SetServerStatus(int server_status);
  void SetExtServerCap(int ext_server_cap);

private:
  int protocol_;
  std::string version_;
  int thread_id_;
  std::string salt_;
  int server_cap_;
  int server_language_;
  int server_status_;
  int ext_server_cap_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
