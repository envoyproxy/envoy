#include "common/secret/secret_manager_impl.h"

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/secret/sds_api.h"
#include "common/secret/secret_provider_impl.h"
#include "common/ssl/certificate_validation_context_config_impl.h"
#include "common/ssl/tls_certificate_config_impl.h"

namespace Envoy {
namespace Secret {

SecretManagerImpl::SecretManagerImpl(Server::ConfigTracker& config_tracker)
    : config_tracker_entry_(config_tracker.add("secrets", [this] { return dumpSecretConfigs(); })) {
}
void SecretManagerImpl::addStaticSecret(const envoy::api::v2::auth::Secret& secret) {
  switch (secret.type_case()) {
  case envoy::api::v2::auth::Secret::TypeCase::kTlsCertificate: {
    auto secret_provider =
        std::make_shared<TlsCertificateConfigProviderImpl>(secret.tls_certificate());
    if (!static_tls_certificate_providers_.insert(std::make_pair(secret.name(), secret_provider))
             .second) {
      throw EnvoyException(
          fmt::format("Duplicate static TlsCertificate secret name {}", secret.name()));
    }
    break;
  }
  case envoy::api::v2::auth::Secret::TypeCase::kValidationContext: {
    auto secret_provider = std::make_shared<CertificateValidationContextConfigProviderImpl>(
        secret.validation_context());
    if (!static_certificate_validation_context_providers_
             .insert(std::make_pair(secret.name(), secret_provider))
             .second) {
      throw EnvoyException(fmt::format(
          "Duplicate static CertificateValidationContext secret name {}", secret.name()));
    }
    break;
  }
  default:
    throw EnvoyException("Secret type not implemented");
  }
}

TlsCertificateConfigProviderSharedPtr
SecretManagerImpl::findStaticTlsCertificateProvider(const std::string& name) const {
  auto secret = static_tls_certificate_providers_.find(name);
  return (secret != static_tls_certificate_providers_.end()) ? secret->second : nullptr;
}

CertificateValidationContextConfigProviderSharedPtr
SecretManagerImpl::findStaticCertificateValidationContextProvider(const std::string& name) const {
  auto secret = static_certificate_validation_context_providers_.find(name);
  return (secret != static_certificate_validation_context_providers_.end()) ? secret->second
                                                                            : nullptr;
}

TlsCertificateConfigProviderSharedPtr SecretManagerImpl::createInlineTlsCertificateProvider(
    const envoy::api::v2::auth::TlsCertificate& tls_certificate) {
  return std::make_shared<TlsCertificateConfigProviderImpl>(tls_certificate);
}

CertificateValidationContextConfigProviderSharedPtr
SecretManagerImpl::createInlineCertificateValidationContextProvider(
    const envoy::api::v2::auth::CertificateValidationContext& certificate_validation_context) {
  return std::make_shared<CertificateValidationContextConfigProviderImpl>(
      certificate_validation_context);
}

TlsCertificateConfigProviderSharedPtr SecretManagerImpl::findOrCreateTlsCertificateProvider(
    const envoy::api::v2::core::ConfigSource& sds_config_source, const std::string& config_name,
    Server::Configuration::TransportSocketFactoryContext& secret_provider_context) {
  return certificate_providers_.findOrCreate(sds_config_source, config_name,
                                             secret_provider_context);
}

CertificateValidationContextConfigProviderSharedPtr
SecretManagerImpl::findOrCreateCertificateValidationContextProvider(
    const envoy::api::v2::core::ConfigSource& sds_config_source, const std::string& config_name,
    Server::Configuration::TransportSocketFactoryContext& secret_provider_context) {
  return validation_context_providers_.findOrCreate(sds_config_source, config_name,
                                                    secret_provider_context);
}

ProtobufTypes::MessagePtr SecretManagerImpl::dumpSecretConfigs() {
  auto config_dump = std::make_unique<envoy::admin::v2alpha::SecretsConfigDump>();
  auto providers = certificate_providers_.allSecretProviders();
  for (const auto& cert_secrets : providers) {
    const auto& secret_data = cert_secrets->secretData();
    const auto& tls_cert = cert_secrets->secret();
    ::envoy::admin::v2alpha::SecretsConfigDump_DynamicSecret* dump_secret;
    bool secret_ready = tls_cert != nullptr;
    if (secret_ready) {
      dump_secret = config_dump->mutable_dynamic_active_secrets()->Add();
    } else {
      dump_secret = config_dump->mutable_dynamic_warming_secrets()->Add();
    }
    auto secret = dump_secret->mutable_secret();

    ProtobufWkt::Timestamp last_updated_ts;
    TimestampUtil::systemClockToTimestamp(secret_data.last_updated_, last_updated_ts);
    dump_secret->set_version_info(secret_data.version_info_);
    *dump_secret->mutable_last_updated() = last_updated_ts;
    secret->set_name(secret_data.resource_name);
    if (secret_ready) {
      auto tls_certificate = secret->mutable_tls_certificate();
      tls_certificate->MergeFrom(*tls_cert);
      // We clear private key and password to avoid information leaking.j
      // TODO(incfly): switch to more generic scrubbing mechanism once
      // https://github.com/envoyproxy/envoy/issues/4757 is resolved.
      tls_certificate->clear_private_key();
      tls_certificate->clear_password();
    }
  }

  // Handling validation Context provided via SDS.
  auto context_secret_provider = validation_context_providers_.allSecretProviders();
  for (const auto& validation_context_secret : context_secret_provider) {
    const auto& secret_data = validation_context_secret->secretData();
    const auto& validation_context = validation_context_secret->secret();
    ::envoy::admin::v2alpha::SecretsConfigDump_DynamicSecret* dump_secret;
    bool secret_ready = validation_context != nullptr;
    if (secret_ready) {
      dump_secret = config_dump->mutable_dynamic_active_secrets()->Add();
    } else {
      dump_secret = config_dump->mutable_dynamic_warming_secrets()->Add();
    }
    auto secret = dump_secret->mutable_secret();
    ProtobufWkt::Timestamp last_updated_ts;
    TimestampUtil::systemClockToTimestamp(secret_data.last_updated_, last_updated_ts);
    dump_secret->set_version_info(secret_data.version_info_);
    *dump_secret->mutable_last_updated() = last_updated_ts;
    secret->set_name(secret_data.resource_name);
    if (secret_ready) {
      secret->mutable_validation_context()->MergeFrom(*validation_context);
    }
  }
  return config_dump;
}

} // namespace Secret
} // namespace Envoy
