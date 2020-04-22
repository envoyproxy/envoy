#pragma once

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
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

  MOCK_METHOD(void, addStaticSecret,
              (const envoy::extensions::transport_sockets::tls::v3::Secret& secret));
  MOCK_METHOD(TlsCertificateConfigProviderSharedPtr, findStaticTlsCertificateProvider,
              (const std::string& name), (const));
  MOCK_METHOD(CertificateValidationContextConfigProviderSharedPtr,
              findStaticCertificateValidationContextProvider, (const std::string& name), (const));
  MOCK_METHOD(TlsSessionTicketKeysConfigProviderSharedPtr,
              findStaticTlsSessionTicketKeysContextProvider, (const std::string& name), (const));
  MOCK_METHOD(GenericSecretConfigProviderSharedPtr, findStaticGenericSecretProvider,
              (const std::string& name), (const));
  MOCK_METHOD(
      TlsCertificateConfigProviderSharedPtr, createInlineTlsCertificateProvider,
      (const envoy::extensions::transport_sockets::tls::v3::TlsCertificate& tls_certificate));
  MOCK_METHOD(CertificateValidationContextConfigProviderSharedPtr,
              createInlineCertificateValidationContextProvider,
              (const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext&
                   certificate_validation_context));
  MOCK_METHOD(TlsSessionTicketKeysConfigProviderSharedPtr, createInlineTlsSessionTicketKeysProvider,
              (const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys&
                   tls_session_ticket_keys));
  MOCK_METHOD(GenericSecretConfigProviderSharedPtr, createInlineGenericSecretProvider,
              (const envoy::extensions::transport_sockets::tls::v3::GenericSecret& generic_secret));
  MOCK_METHOD(TlsCertificateConfigProviderSharedPtr, findOrCreateTlsCertificateProvider,
              (const envoy::config::core::v3::ConfigSource&, const std::string&,
               Server::Configuration::TransportSocketFactoryContext&));
  MOCK_METHOD(CertificateValidationContextConfigProviderSharedPtr,
              findOrCreateCertificateValidationContextProvider,
              (const envoy::config::core::v3::ConfigSource& config_source,
               const std::string& config_name,
               Server::Configuration::TransportSocketFactoryContext& secret_provider_context));
  MOCK_METHOD(TlsSessionTicketKeysConfigProviderSharedPtr,
              findOrCreateTlsSessionTicketKeysContextProvider,
              (const envoy::config::core::v3::ConfigSource&, const std::string&,
               Server::Configuration::TransportSocketFactoryContext&));
  MOCK_METHOD(GenericSecretConfigProviderSharedPtr, findOrCreateGenericSecretProvider,
              (const envoy::config::core::v3::ConfigSource&, const std::string&,
               Server::Configuration::TransportSocketFactoryContext&));
};

class MockSecretCallbacks : public SecretCallbacks {
public:
  MockSecretCallbacks();
  ~MockSecretCallbacks() override;
  MOCK_METHOD(void, onAddOrUpdateSecret, ());
};

} // namespace Secret
} // namespace Envoy
