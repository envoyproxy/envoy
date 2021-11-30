#pragma once

#include <string>

#include "envoy/api/api.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/tls_root_ca_certificate_config.h"

namespace Envoy {
namespace Ssl {

class TlsRootCACertificateConfigImpl : public TlsRootCACertificateConfig {
public:
  TlsRootCACertificateConfigImpl(
      const envoy::extensions::transport_sockets::tls::v3::TlsRootCACertificate& config,
      // Server::Configuration::TransportSocketFactoryContext& factory_context,
      Api::Api& api);

  const std::string& cert() const override { return cert_; }
  const std::string& certPath() const override { return cert_path_; }
  const std::string& privateKey() const override { return private_key_; }
  const std::string& privateKeyPath() const override { return private_key_path_; }

private:
  const std::string cert_;
  const std::string cert_path_;
  const std::string private_key_;
  const std::string private_key_path_;
};

} // namespace Ssl
} // namespace Envoy
