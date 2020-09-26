#pragma once

#include <string>

#include "envoy/api/api.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/tls_certificate_config.h"

namespace Envoy {
namespace Ssl {

class TlsCertificateConfigImpl : public TlsCertificateConfig {
public:
  TlsCertificateConfigImpl(
      const envoy::extensions::transport_sockets::tls::v3::TlsCertificate& config,
      Server::Configuration::TransportSocketFactoryContext* factory_context, Api::Api& api);

  const std::string& certificateChain() const override { return certificate_chain_; }
  const std::string& certificateChainPath() const override { return certificate_chain_path_; }
  const std::string& privateKey() const override { return private_key_; }
  const std::string& privateKeyPath() const override { return private_key_path_; }
  const std::string& password() const override { return password_; }
  const std::string& passwordPath() const override { return password_path_; }
  const std::vector<uint8_t>& ocspStaple() const override { return ocsp_staple_; }
  const std::string& ocspStaplePath() const override { return ocsp_staple_path_; }
  Envoy::Ssl::PrivateKeyMethodProviderSharedPtr privateKeyMethod() const override {
    return private_key_method_;
  }

private:
  const std::string certificate_chain_;
  const std::string certificate_chain_path_;
  const std::string private_key_;
  const std::string private_key_path_;
  const std::string password_;
  const std::string password_path_;
  const std::vector<uint8_t> ocsp_staple_;
  const std::string ocsp_staple_path_;
  Envoy::Ssl::PrivateKeyMethodProviderSharedPtr private_key_method_{};
};

} // namespace Ssl
} // namespace Envoy
