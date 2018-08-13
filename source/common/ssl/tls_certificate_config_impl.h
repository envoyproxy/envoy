#pragma once

#include <string>

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/ssl/tls_certificate_config.h"

namespace Envoy {
namespace Ssl {

class TlsCertificateConfigImpl : public TlsCertificateConfig {
public:
  TlsCertificateConfigImpl(const envoy::api::v2::auth::TlsCertificate& config);

  const std::string& certificateChain() const override { return certificate_chain_; }
  const std::string& certificateChainPath() const override { return certificate_chain_path_; }
  const std::string& privateKey() const override { return private_key_; }
  const std::string& privateKeyPath() const override { return private_key_path_; }

private:
  const std::string certificate_chain_;
  const std::string certificate_chain_path_;
  const std::string private_key_;
  const std::string private_key_path_;
};

} // namespace Ssl
} // namespace Envoy
