#pragma once

#include <string>
#include <vector>

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/ssl/context_config.h"

#include "common/json/json_loader.h"

namespace Envoy {
namespace Ssl {

static const std::string INLINE_STRING = "<inline>";

class ContextConfigImpl : public virtual Ssl::ContextConfig {
public:
  // Ssl::ContextConfig
  const std::string& alpnProtocols() const override { return alpn_protocols_; }
  const std::string& altAlpnProtocols() const override { return alt_alpn_protocols_; }
  const std::string& cipherSuites() const override { return cipher_suites_; }
  const std::string& ecdhCurves() const override { return ecdh_curves_; }
  const std::string& caCert() const override { return ca_cert_; }
  const std::string& caCertPath() const override {
    return (ca_cert_path_.empty() && !ca_cert_.empty()) ? INLINE_STRING : ca_cert_path_;
  }
  const std::string& certificateRevocationList() const override {
    return certificate_revocation_list_;
  }
  const std::string& certificateRevocationListPath() const override {
    return (certificate_revocation_list_path_.empty() && !certificate_revocation_list_.empty())
               ? INLINE_STRING
               : certificate_revocation_list_path_;
  }
  const std::string& certChain() const override { return cert_chain_; }
  const std::string& certChainPath() const override {
    return (cert_chain_path_.empty() && !cert_chain_.empty()) ? INLINE_STRING : cert_chain_path_;
  }
  const std::string& privateKey() const override { return private_key_; }
  const std::string& privateKeyPath() const override {
    return (private_key_path_.empty() && !private_key_.empty()) ? INLINE_STRING : private_key_path_;
  }
  const std::vector<std::string>& verifySubjectAltNameList() const override {
    return verify_subject_alt_name_list_;
  };
  const std::vector<std::string>& verifyCertificateHashList() const override {
    return verify_certificate_hash_list_;
  };
  unsigned minProtocolVersion() const override { return min_protocol_version_; };
  unsigned maxProtocolVersion() const override { return max_protocol_version_; };

protected:
  ContextConfigImpl(const envoy::api::v2::auth::CommonTlsContext& config);

private:
  static unsigned
  tlsVersionFromProto(const envoy::api::v2::auth::TlsParameters_TlsProtocol& version,
                      unsigned default_version);

  static const std::string DEFAULT_CIPHER_SUITES;
  static const std::string DEFAULT_ECDH_CURVES;

  const std::string alpn_protocols_;
  const std::string alt_alpn_protocols_;
  const std::string cipher_suites_;
  const std::string ecdh_curves_;
  const std::string ca_cert_;
  const std::string ca_cert_path_;
  const std::string certificate_revocation_list_;
  const std::string certificate_revocation_list_path_;
  const std::string cert_chain_;
  const std::string cert_chain_path_;
  const std::string private_key_;
  const std::string private_key_path_;
  const std::vector<std::string> verify_subject_alt_name_list_;
  const std::vector<std::string> verify_certificate_hash_list_;
  const unsigned min_protocol_version_;
  const unsigned max_protocol_version_;
};

class ClientContextConfigImpl : public ContextConfigImpl, public ClientContextConfig {
public:
  explicit ClientContextConfigImpl(const envoy::api::v2::auth::UpstreamTlsContext& config);
  explicit ClientContextConfigImpl(const Json::Object& config);

  // Ssl::ClientContextConfig
  const std::string& serverNameIndication() const override { return server_name_indication_; }

private:
  const std::string server_name_indication_;
};

class ServerContextConfigImpl : public ContextConfigImpl, public ServerContextConfig {
public:
  explicit ServerContextConfigImpl(const envoy::api::v2::auth::DownstreamTlsContext& config);
  explicit ServerContextConfigImpl(const Json::Object& config);

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
