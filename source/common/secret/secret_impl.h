#pragma once

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/secret/secret.h"

namespace Envoy {
namespace Secret {

class SecretImpl : public Secret {
public:
  SecretImpl(const envoy::api::v2::auth::Secret& config);

  const std::string& getName() override { return name_; }

  const std::string& getCertificateChain() override { return certificate_chain_; }

  const std::string& getPrivateKey() override { return private_key_; }

private:
  std::string name_;
  std::string certificate_chain_;
  std::string private_key_;
};

} // namespace Secret
} // namespace Envoy
