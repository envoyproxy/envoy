#pragma once

#include "envoy/secret/secret_manager.h"
#include "envoy/ssl/tls_certificate_config.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Secret {

class MockSecretManager : public SecretManager {
public:
  MockSecretManager();
  ~MockSecretManager();

  MOCK_METHOD1(addOrUpdateSecret, void(const envoy::api::v2::auth::Secret& secret));
  MOCK_CONST_METHOD1(findTlsCertificate, const Ssl::TlsCertificateConfig*(const std::string& name));
};

} // namespace Secret
} // namespace Envoy
