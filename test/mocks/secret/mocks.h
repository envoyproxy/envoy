#pragma once

#include "envoy/secret/secret_callbacks.h"
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

  MOCK_METHOD1(addStaticSecret, void(const envoy::api::v2::auth::Secret& secret));
  MOCK_CONST_METHOD1(findStaticTlsCertificateProvider,
                     TlsCertificateConfigProviderSharedPtr(const std::string& name));
  MOCK_CONST_METHOD1(findStaticCertificateValidationContextProvider,
                     CertificateValidationContextConfigProviderSharedPtr(const std::string& name));
  MOCK_METHOD1(createInlineTlsCertificateProvider,
               TlsCertificateConfigProviderSharedPtr(
                   const envoy::api::v2::auth::TlsCertificate& tls_certificate));
  MOCK_METHOD1(createInlineCertificateValidationContextProvider,
               CertificateValidationContextConfigProviderSharedPtr(
                   const envoy::api::v2::auth::CertificateValidationContext&
                       certificate_validation_context));
};

class MockSecretCallbacks : public SecretCallbacks {
public:
  MockSecretCallbacks();
  ~MockSecretCallbacks();
  MOCK_METHOD0(onAddOrUpdateSecret, void());
};

} // namespace Secret
} // namespace Envoy
