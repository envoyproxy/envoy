#pragma once

#include <memory>
#include <string>

#include "test/integration/http_integration.h"
#include "test/integration/server.h"
#include "test/integration/ssl_utility.h"
#include "test/mocks/secret/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Ssl {

class SslIntegrationTestBase : public HttpIntegrationTest {
public:
  SslIntegrationTestBase(Network::Address::IpVersion ip_version)
      : HttpIntegrationTest(Http::CodecType::HTTP1, ip_version) {}

  void initialize() override;

  void TearDown();

  Network::ClientConnectionPtr makeSslConn() { return makeSslClientConnection({}); }
  virtual Network::ClientConnectionPtr
  makeSslClientConnection(const ClientSslTransportOptions& options);
  void checkStats();

protected:
  bool server_tlsv1_3_{false};
  std::vector<std::string> server_curves_;
  std::vector<std::string> server_ciphers_;
  bool server_rsa_cert_{true};
  bool server_rsa_cert_ocsp_staple_{false};
  bool server_ecdsa_cert_{false};
  std::string server_ecdsa_cert_name_{"server_ecdsa"};
  bool server_ecdsa_cert_ocsp_staple_{false};
  bool ocsp_staple_required_{false};
  bool prefer_client_ciphers_{false};
  bool client_ecdsa_cert_{false};
  std::string tls_cert_selector_yaml_{""};
  // Set this true to debug SSL handshake issues with openssl s_client. The
  // verbose trace will be in the logs, openssl must be installed separately.
  bool debug_with_s_client_{false};
  bool keylog_local_{false};
  bool keylog_remote_{false};
  bool keylog_local_negative_{false};
  bool keylog_remote_negative_{false};
  bool keylog_multiple_ips_{false};
  std::string keylog_path_;
  std::unique_ptr<ContextManager> context_manager_;
};

} // namespace Ssl
} // namespace Envoy
