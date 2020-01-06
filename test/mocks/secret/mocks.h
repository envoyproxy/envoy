#pragma once

#include "envoy/config/core/v3alpha/config_source.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3alpha/cert.pb.h"
#include "envoy/secret/secret_callbacks.h"
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
  ~MockSecretManager() override;

  MOCK_METHOD1(addStaticSecret,
               void(const envoy::extensions::transport_sockets::tls::v3alpha::Secret& secret));
  MOCK_CONST_METHOD1(findStaticTlsCertificateProvider,
                     TlsCertificateConfigProviderSharedPtr(const std::string& name));
  MOCK_CONST_METHOD1(findStaticCertificateValidationContextProvider,
                     CertificateValidationContextConfigProviderSharedPtr(const std::string& name));
  MOCK_CONST_METHOD1(findStaticTlsSessionTicketKeysContextProvider,
                     TlsSessionTicketKeysConfigProviderSharedPtr(const std::string& name));
  MOCK_METHOD1(createInlineTlsCertificateProvider,
               TlsCertificateConfigProviderSharedPtr(
                   const envoy::extensions::transport_sockets::tls::v3alpha::TlsCertificate&
                       tls_certificate));
  MOCK_METHOD1(
      createInlineCertificateValidationContextProvider,
      CertificateValidationContextConfigProviderSharedPtr(
          const envoy::extensions::transport_sockets::tls::v3alpha::CertificateValidationContext&
              certificate_validation_context));
  MOCK_METHOD1(createInlineTlsSessionTicketKeysProvider,
               TlsSessionTicketKeysConfigProviderSharedPtr(
                   const envoy::extensions::transport_sockets::tls::v3alpha::TlsSessionTicketKeys&
                       tls_session_ticket_keys));
  MOCK_METHOD3(findOrCreateTlsCertificateProvider,
               TlsCertificateConfigProviderSharedPtr(
                   const envoy::config::core::v3alpha::ConfigSource&, const std::string&,
                   Server::Configuration::TransportSocketFactoryContext&));
  MOCK_METHOD3(findOrCreateCertificateValidationContextProvider,
               CertificateValidationContextConfigProviderSharedPtr(
                   const envoy::config::core::v3alpha::ConfigSource& config_source,
                   const std::string& config_name,
                   Server::Configuration::TransportSocketFactoryContext& secret_provider_context));
  MOCK_METHOD3(findOrCreateTlsSessionTicketKeysContextProvider,
               TlsSessionTicketKeysConfigProviderSharedPtr(
                   const envoy::config::core::v3alpha::ConfigSource&, const std::string&,
                   Server::Configuration::TransportSocketFactoryContext&));
};

class MockSecretCallbacks : public SecretCallbacks {
public:
  MockSecretCallbacks();
  ~MockSecretCallbacks() override;
  MOCK_METHOD0(onAddOrUpdateSecret, void());
};

} // namespace Secret
} // namespace Envoy
