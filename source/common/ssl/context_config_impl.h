#pragma once

#include <string>
#include <vector>

#include "envoy/ssl/context_config.h"

#include "common/json/json_loader.h"

#include "api/sds.pb.h"

namespace Envoy {
namespace Ssl {

class ContextConfigImpl : public virtual Ssl::ContextConfig {
public:
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

protected:
  ContextConfigImpl(const envoy::api::v2::CommonTlsContext& config);

private:
  static const std::string DEFAULT_CIPHER_SUITES;
  static const std::string DEFAULT_ECDH_CURVES;

  const std::string alpn_protocols_;
  const std::string alt_alpn_protocols_;
  const std::string cipher_suites_;
  const std::string ecdh_curves_;
  const std::string ca_cert_file_;
  const std::string cert_chain_file_;
  const std::string private_key_file_;
  const std::vector<std::string> verify_subject_alt_name_list_;
  const std::string verify_certificate_hash_;
};

class ClientContextConfigImpl : public ContextConfigImpl, public ClientContextConfig {
public:
  ClientContextConfigImpl(const envoy::api::v2::UpstreamTlsContext& config);
  ClientContextConfigImpl(const Json::Object& config);

  // Ssl::ClientContextConfig
  const std::string& serverNameIndication() const override { return server_name_indication_; }

private:
  const std::string server_name_indication_;
};

class ServerContextConfigImpl : public ContextConfigImpl, public ServerContextConfig {
public:
  ServerContextConfigImpl(const envoy::api::v2::DownstreamTlsContext& config);
  ServerContextConfigImpl(const Json::Object& config);

  // Ssl::ServerContextConfig
  bool requireClientCertificate() const override { return require_client_certificate_; }
  const std::vector<SessionTicketKey>& sessionTicketKeys() const override {
    return session_ticket_keys_;
  }

private:
  const bool require_client_certificate_;
  const std::vector<SessionTicketKey> session_ticket_keys_;

  static void validateAndAppendKey(std::vector<ServerContextConfig::SessionTicketKey>& keys,
                                   const std::string& key_data);
};

} // namespace Ssl
} // namespace Envoy
