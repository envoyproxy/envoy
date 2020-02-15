#pragma once

#include <string>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/secret/secret_provider.h"

namespace Envoy {

namespace Server {
namespace Configuration {
class TransportSocketFactoryContext;
} // namespace Configuration
} // namespace Server

namespace Secret {

/**
 * A manager for static and dynamic secrets.
 */
class SecretManager {
public:
  virtual ~SecretManager() = default;

  /**
   * @param add a static secret from envoy::api::v2::auth::Secret.
   * @throw an EnvoyException if the secret is invalid or not supported, or there is duplicate.
   */
  virtual void
  addStaticSecret(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) PURE;

  /**
   * @param name a name of the static TlsCertificateConfigProvider.
   * @return the TlsCertificateConfigProviderSharedPtr. Returns nullptr if the static secret is not
   * found.
   */
  virtual TlsCertificateConfigProviderSharedPtr
  findStaticTlsCertificateProvider(const std::string& name) const PURE;

  /**
   * @param name a name of the static CertificateValidationContextConfigProviderSharedPtr.
   * @return the CertificateValidationContextConfigProviderSharedPtr. Returns nullptr
   * if the static certificate validation context is not found.
   */
  virtual CertificateValidationContextConfigProviderSharedPtr
  findStaticCertificateValidationContextProvider(const std::string& name) const PURE;

  /**
   * @param name a name of the static TlsSessionTicketKeysConfigProviderSharedPtr.
   * @return the TlsSessionTicketKeysConfigProviderSharedPtr. Returns nullptr
   * if the static tls session ticket keys are not found.
   */
  virtual TlsSessionTicketKeysConfigProviderSharedPtr
  findStaticTlsSessionTicketKeysContextProvider(const std::string& name) const PURE;

  /**
   * @param name a name of the static GenericSecretConfigProvider.
   * @return the GenericSecretConfigProviderSharedPtr. Returns nullptr if the static secret is not
   * found.
   */
  virtual GenericSecretConfigProviderSharedPtr
  findStaticGenericSecretProvider(const std::string& name) const PURE;

  /**
   * @param tls_certificate the protobuf config of the TLS certificate.
   * @return a TlsCertificateConfigProviderSharedPtr created from tls_certificate.
   */
  virtual TlsCertificateConfigProviderSharedPtr createInlineTlsCertificateProvider(
      const envoy::extensions::transport_sockets::tls::v3::TlsCertificate& tls_certificate) PURE;

  /**
   * @param certificate_validation_context the protobuf config of the certificate validation
   * context.
   * @return a CertificateValidationContextConfigProviderSharedPtr created from
   * certificate_validation_context.
   */
  virtual CertificateValidationContextConfigProviderSharedPtr
  createInlineCertificateValidationContextProvider(
      const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext&
          certificate_validation_context) PURE;

  /**
   * @param tls_certificate the protobuf config of the TLS session ticket keys.
   * @return a TlsSessionTicketKeysConfigProviderSharedPtr created from session_ticket_keys.
   */
  virtual TlsSessionTicketKeysConfigProviderSharedPtr createInlineTlsSessionTicketKeysProvider(
      const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys& tls_certificate)
      PURE;

  /**
   * @param generic_secret the protobuf config of the generic secret.
   * @return a GenericSecretConfigProviderSharedPtr created from tls_certificate.
   */
  virtual GenericSecretConfigProviderSharedPtr createInlineGenericSecretProvider(
      const envoy::extensions::transport_sockets::tls::v3::GenericSecret& generic_secret) PURE;

  /**
   * Finds and returns a dynamic secret provider associated to SDS config. Create
   * a new one if such provider does not exist.
   *
   * @param config_source a protobuf message object containing a SDS config source.
   * @param config_name a name that uniquely refers to the SDS config source.
   * @param secret_provider_context context that provides components for creating and initializing
   * secret provider.
   * @return TlsCertificateConfigProviderSharedPtr the dynamic TLS secret provider.
   */
  virtual TlsCertificateConfigProviderSharedPtr findOrCreateTlsCertificateProvider(
      const envoy::config::core::v3::ConfigSource& config_source, const std::string& config_name,
      Server::Configuration::TransportSocketFactoryContext& secret_provider_context) PURE;

  /**
   * Finds and returns a dynamic secret provider associated to SDS config. Create
   * a new one if such provider does not exist.
   *
   * @param config_source a protobuf message object containing a SDS config source.
   * @param config_name a name that uniquely refers to the SDS config source.
   * @param secret_provider_context context that provides components for creating and initializing
   * secret provider.
   * @return CertificateValidationContextConfigProviderSharedPtr the dynamic certificate validation
   * context secret provider.
   */
  virtual CertificateValidationContextConfigProviderSharedPtr
  findOrCreateCertificateValidationContextProvider(
      const envoy::config::core::v3::ConfigSource& config_source, const std::string& config_name,
      Server::Configuration::TransportSocketFactoryContext& secret_provider_context) PURE;

  /**
   * Finds and returns a dynamic secret provider associated to SDS config. Create
   * a new one if such provider does not exist.
   *
   * @param config_source a protobuf message object containing a SDS config source.
   * @param config_name a name that uniquely refers to the SDS config source.
   * @param secret_provider_context context that provides components for creating and initializing
   * secret provider.
   * @return TlsSessionTicketKeysConfigProviderSharedPtr the dynamic tls session ticket keys secret
   * provider.
   */
  virtual TlsSessionTicketKeysConfigProviderSharedPtr
  findOrCreateTlsSessionTicketKeysContextProvider(
      const envoy::config::core::v3::ConfigSource& config_source, const std::string& config_name,
      Server::Configuration::TransportSocketFactoryContext& secret_provider_context) PURE;

  /**
   * Finds and returns a dynamic secret provider associated to SDS config. Create a new one if such
   * provider does not exist.
   *
   * @param config_source a protobuf message object containing a SDS config source.
   * @param config_name a name that uniquely refers to the SDS config source.
   * @param secret_provider_context context that provides components for creating and initializing
   * secret provider.
   * @return GenericSecretConfigProviderSharedPtr the dynamic generic secret provider.
   */
  virtual GenericSecretConfigProviderSharedPtr findOrCreateGenericSecretProvider(
      const envoy::config::core::v3::ConfigSource& config_source, const std::string& config_name,
      Server::Configuration::TransportSocketFactoryContext& secret_provider_context) PURE;
};

} // namespace Secret
} // namespace Envoy
