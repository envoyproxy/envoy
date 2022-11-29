#pragma once

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/certificate_validation_context_config.h"
#include "envoy/ssl/tls_certificate_config.h"

#include "source/common/common/logger.h"
#include "source/common/secret/sds_api.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Secret {

class SecretManagerImpl : public SecretManager {
public:
  SecretManagerImpl(OptRef<Server::ConfigTracker> config_tracker);
  void
  addStaticSecret(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) override;

  TlsCertificateConfigProviderSharedPtr
  findStaticTlsCertificateProvider(const std::string& name) const override;

  CertificateValidationContextConfigProviderSharedPtr
  findStaticCertificateValidationContextProvider(const std::string& name) const override;

  TlsSessionTicketKeysConfigProviderSharedPtr
  findStaticTlsSessionTicketKeysContextProvider(const std::string& name) const override;

  GenericSecretConfigProviderSharedPtr
  findStaticGenericSecretProvider(const std::string& name) const override;

  TlsCertificateConfigProviderSharedPtr createInlineTlsCertificateProvider(
      const envoy::extensions::transport_sockets::tls::v3::TlsCertificate& tls_certificate)
      override;

  CertificateValidationContextConfigProviderSharedPtr
  createInlineCertificateValidationContextProvider(
      const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext&
          certificate_validation_context) override;

  TlsSessionTicketKeysConfigProviderSharedPtr createInlineTlsSessionTicketKeysProvider(
      const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys&
          tls_session_ticket_keys) override;

  GenericSecretConfigProviderSharedPtr createInlineGenericSecretProvider(
      const envoy::extensions::transport_sockets::tls::v3::GenericSecret& generic_secret) override;

  TlsCertificateConfigProviderSharedPtr findOrCreateTlsCertificateProvider(
      const envoy::config::core::v3::ConfigSource& config_source, const std::string& config_name,
      Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
      Init::Manager& init_manager) override;

  CertificateValidationContextConfigProviderSharedPtr
  findOrCreateCertificateValidationContextProvider(
      const envoy::config::core::v3::ConfigSource& config_source, const std::string& config_name,
      Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
      Init::Manager& init_manager) override;

  TlsSessionTicketKeysConfigProviderSharedPtr findOrCreateTlsSessionTicketKeysContextProvider(
      const envoy::config::core::v3::ConfigSource& config_source, const std::string& config_name,
      Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
      Init::Manager& init_manager) override;

  GenericSecretConfigProviderSharedPtr findOrCreateGenericSecretProvider(
      const envoy::config::core::v3::ConfigSource& config_source, const std::string& config_name,
      Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
      Init::Manager& init_manager) override;

private:
  ProtobufTypes::MessagePtr dumpSecretConfigs(const Matchers::StringMatcher& name_matcher);

  template <class SecretType>
  class DynamicSecretProviders : public Logger::Loggable<Logger::Id::secret> {
  public:
    // Finds or creates SdsApi object.
    std::shared_ptr<SecretType>
    findOrCreate(const envoy::config::core::v3::ConfigSource& sds_config_source,
                 const std::string& config_name,
                 Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
                 Init::Manager& init_manager) {
      const std::string map_key =
          absl::StrCat(MessageUtil::hash(sds_config_source), ".", config_name);

      std::shared_ptr<SecretType> secret_provider = dynamic_secret_providers_[map_key].lock();
      if (!secret_provider) {
        // SdsApi is owned by ListenerImpl and ClusterInfo which are destroyed before
        // SecretManagerImpl. It is safe to invoke this callback at the destructor of SdsApi.
        std::function<void()> unregister_secret_provider = [map_key, this]() {
          removeDynamicSecretProvider(map_key);
        };
        secret_provider = SecretType::create(secret_provider_context, sds_config_source,
                                             config_name, unregister_secret_provider);
        dynamic_secret_providers_[map_key] = secret_provider;
      }
      // It is important to add the init target to the manager regardless the secret provider is new
      // or existing. Different clusters / listeners can share same secret so they have to be marked
      // warming correctly.

      // Note that we are not using secret_provider_context's init manager because in some cases,
      // for example oauth2 filter with sds config, it could be server's init manager. In oauth2
      // filter example, if the filter config is dynamic, it could be received from xds server when
      // the server's init manager is already in the initialized state. In that situation, adding
      // init target to the initialized init manager will lead to assertion failure.
      //
      // It is expected that correct init manager will be passed to this method by the caller
      // separately.
      init_manager.add(*secret_provider->initTarget());
      return secret_provider;
    }

    std::vector<std::shared_ptr<SecretType>> allSecretProviders() {
      std::vector<std::shared_ptr<SecretType>> providers;
      for (const auto& secret_entry : dynamic_secret_providers_) {
        std::shared_ptr<SecretType> secret_provider = secret_entry.second.lock();
        if (secret_provider) {
          providers.push_back(std::move(secret_provider));
        }
      }
      return providers;
    }

  private:
    // Removes dynamic secret provider which has been deleted.
    void removeDynamicSecretProvider(const std::string& map_key) {
      ENVOY_LOG(debug, "Unregister secret provider. hash key: {}", map_key);

      auto num_deleted = dynamic_secret_providers_.erase(map_key);
      ASSERT(num_deleted == 1, "");
    }

    absl::node_hash_map<std::string, std::weak_ptr<SecretType>> dynamic_secret_providers_;
  };

  // Manages pairs of secret name and TlsCertificateConfigProviderSharedPtr.
  absl::node_hash_map<std::string, TlsCertificateConfigProviderSharedPtr>
      static_tls_certificate_providers_;

  // Manages pairs of secret name and CertificateValidationContextConfigProviderSharedPtr.
  absl::node_hash_map<std::string, CertificateValidationContextConfigProviderSharedPtr>
      static_certificate_validation_context_providers_;

  absl::node_hash_map<std::string, TlsSessionTicketKeysConfigProviderSharedPtr>
      static_session_ticket_keys_providers_;

  // Manages pairs of secret name and GenericSecretConfigProviderSharedPtr.
  absl::node_hash_map<std::string, GenericSecretConfigProviderSharedPtr>
      static_generic_secret_providers_;

  // map hash code of SDS config source and SdsApi object.
  DynamicSecretProviders<TlsCertificateSdsApi> certificate_providers_;
  DynamicSecretProviders<CertificateValidationContextSdsApi> validation_context_providers_;
  DynamicSecretProviders<TlsSessionTicketKeysSdsApi> session_ticket_keys_providers_;
  DynamicSecretProviders<GenericSecretSdsApi> generic_secret_providers_;

  Server::ConfigTracker::EntryOwnerPtr config_tracker_entry_;
};

} // namespace Secret
} // namespace Envoy
