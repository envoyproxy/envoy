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

  MOCK_METHOD(absl::Status, addStaticSecret,
              (const envoy::extensions::transport_sockets::tls::v3::Secret& secret), (override));
  MOCK_METHOD(TlsCertificateConfigProviderSharedPtr, findStaticTlsCertificateProvider,
              (const std::string& name), (const, override));
  MOCK_METHOD(CertificateValidationContextConfigProviderSharedPtr,
              findStaticCertificateValidationContextProvider, (const std::string& name),
              (const, override));
  MOCK_METHOD(TlsSessionTicketKeysConfigProviderSharedPtr,
              findStaticTlsSessionTicketKeysContextProvider, (const std::string& name),
              (const, override));
  MOCK_METHOD(GenericSecretConfigProviderSharedPtr, findStaticGenericSecretProvider,
              (const std::string& name), (const, override));
  MOCK_METHOD(
      TlsCertificateConfigProviderSharedPtr, createInlineTlsCertificateProvider,
      (const envoy::extensions::transport_sockets::tls::v3::TlsCertificate& tls_certificate),
      (override));
  MOCK_METHOD(CertificateValidationContextConfigProviderSharedPtr,
              createInlineCertificateValidationContextProvider,
              (const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext&
                   certificate_validation_context),
              (override));
  MOCK_METHOD(TlsSessionTicketKeysConfigProviderSharedPtr, createInlineTlsSessionTicketKeysProvider,
              (const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys&
                   tls_session_ticket_keys),
              (override));
  MOCK_METHOD(GenericSecretConfigProviderSharedPtr, createInlineGenericSecretProvider,
              (const envoy::extensions::transport_sockets::tls::v3::GenericSecret& generic_secret),
              (override));
  MOCK_METHOD(TlsCertificateConfigProviderSharedPtr, findOrCreateTlsCertificateProvider,
              (const envoy::config::core::v3::ConfigSource&, const std::string&,
               Server::Configuration::ServerFactoryContext&, OptRef<Init::Manager> init_manager,
               bool warm),
              (override));
  MOCK_METHOD(CertificateValidationContextConfigProviderSharedPtr,
              findOrCreateCertificateValidationContextProvider,
              (const envoy::config::core::v3::ConfigSource& config_source,
               const std::string& config_name,
               Server::Configuration::ServerFactoryContext& server_context,
               Init::Manager& init_manager),
              (override));
  MOCK_METHOD(TlsSessionTicketKeysConfigProviderSharedPtr,
              findOrCreateTlsSessionTicketKeysContextProvider,
              (const envoy::config::core::v3::ConfigSource&, const std::string&,
               Server::Configuration::ServerFactoryContext&, Init::Manager& init_manager),
              (override));
  MOCK_METHOD(GenericSecretConfigProviderSharedPtr, findOrCreateGenericSecretProvider,
              (const envoy::config::core::v3::ConfigSource&, const std::string&,
               Server::Configuration::ServerFactoryContext&, OptRef<Init::Manager> init_manager,
               bool warm),
              (override));
  MOCK_METHOD(std::vector<absl::string_view>, dynamicActiveSecretNames, (), (const, override));
};

class MockSecretCallbacks : public SecretCallbacks {
public:
  MockSecretCallbacks();
  ~MockSecretCallbacks() override;
  MOCK_METHOD(absl::Status, onAddOrUpdateSecret, (), (override));
};

class MockGenericSecretConfigProvider : public GenericSecretConfigProvider {
public:
  MockGenericSecretConfigProvider();
  ~MockGenericSecretConfigProvider() override;

  using SecretType = envoy::extensions::transport_sockets::tls::v3::GenericSecret;

  MOCK_METHOD(SecretType*, secret, (), (const, override));
  MOCK_METHOD(Common::CallbackHandlePtr, addValidationCallback,
              (std::function<absl::Status(const SecretType&)>), (override));
  MOCK_METHOD(Common::CallbackHandlePtr, addUpdateCallback, (std::function<absl::Status()>),
              (override));
  MOCK_METHOD(Common::CallbackHandlePtr, addRemoveCallback, (std::function<absl::Status()>),
              (override));
  MOCK_METHOD(const Init::Target*, initTarget, (), (override));
  void start() override {}
};

} // namespace Secret
} // namespace Envoy
