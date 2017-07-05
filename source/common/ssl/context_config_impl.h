#pragma once

#include <string>
#include <vector>

#include "envoy/ssl/context_config.h"

#include "common/json/json_loader.h"

namespace Envoy {
namespace Ssl {

class ContextConfigImpl final : public Ssl::ContextConfig {
public:
  ContextConfigImpl(const Json::Object& config);

  // Ssl::ContextConfig
  const std::string& alpnProtocols() const override { return alpn_protocols_; }
  const std::string& altAlpnProtocols() const override { return alt_alpn_protocols_; }
  const std::string& cipherSuites() const override { return cipher_suites_; }
  const std::string& ecdhCurves() const override { return ecdh_curves_; }
  const std::string& caCertFile() const override { return ca_cert_file_; }
  const std::string& certChainFile() const override { return cert_chain_file_; }
  const std::string& privateKeyFile() const override { return private_key_file_; }
  const std::vector<std::string>& verifySubjectAltNameList() const override {
    return verify_subject_alt_name_list_;
  };
  const std::string& verifyCertificateHash() const override { return verify_certificate_hash_; };
  const std::string& serverNameIndication() const override { return server_name_indication_; }

private:
  static const std::string DEFAULT_CIPHER_SUITES;
  static const std::string DEFAULT_ECDH_CURVES;

  std::string alpn_protocols_;
  std::string alt_alpn_protocols_;
  std::string cipher_suites_;
  std::string ecdh_curves_;
  std::string ca_cert_file_;
  std::string cert_chain_file_;
  std::string private_key_file_;
  std::vector<std::string> verify_subject_alt_name_list_;
  std::string verify_certificate_hash_;
  std::string server_name_indication_;
};

} // namespace Ssl
} // namespace Envoy
