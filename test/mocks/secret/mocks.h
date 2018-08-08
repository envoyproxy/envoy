#pragma once

#include "envoy/secret/secret_manager.h"
#include "envoy/server/transport_socket_config.h"
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
  MOCK_CONST_METHOD1(findStaticTlsCertificate,
                     const Ssl::TlsCertificateConfig*(const std::string& name));
  MOCK_METHOD3(findOrCreateDynamicSecretProvider,
               DynamicTlsCertificateSecretProviderSharedPtr(
                   const envoy::api::v2::core::ConfigSource&, const std::string&,
                   Server::Configuration::TransportSocketFactoryContext&));
  MOCK_METHOD1(removeDeletedSecretProvider, void(const std::string&));
};

} // namespace Secret
} // namespace Envoy
