#pragma once

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/secret/secret.h"

namespace Envoy {
namespace Secret {

typedef std::unordered_map<std::string, SecretSharedPtr> SecretSharedPtrMap;

class SecretImpl : public Secret {
public:
  SecretImpl(const envoy::api::v2::auth::Secret& config);

  const std::string& name() const override { return name_; }

  const std::string& certificateChain() const override { return certificate_chain_; }

  const std::string& privateKey() const override { return private_key_; }

private:
  std::string name_;
  std::string certificate_chain_;
  std::string private_key_;
};

} // namespace Secret
} // namespace Envoy
