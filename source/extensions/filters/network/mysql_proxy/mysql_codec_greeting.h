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
  int parseMessage(Buffer::Instance& buffer, uint32_t len) override;
  std::string encode() override;

  int getProtocol() const { return protocol_; }
  const std::string& getVersion() const { return version_; }
  int getThreadId() const { return thread_id_; }
  const std::string& getSalt() const { return salt_; };
  int getServerCap() const { return server_cap_; }
  int getServerLanguage() const { return server_language_; }
  int getServerStatus() const { return server_status_; }
  int getExtServerCap() const { return ext_server_cap_; }
  void setProtocol(int protocol);
  void setVersion(std::string& version);
  void setThreadId(int thread_id);
  void setSalt(std::string& salt);
  void setServerCap(int server_cap);
  void setServerLanguage(int server_language);
  void setServerStatus(int server_status);
  void setExtServerCap(int ext_server_cap);

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
