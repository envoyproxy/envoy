#include "source/common/secret/secret_manager_impl.h"

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/common/exception.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/protobuf/utility.h"
#include "source/common/secret/sds_api.h"
#include "source/common/secret/secret_provider_impl.h"
#include "source/common/ssl/certificate_validation_context_config_impl.h"
#include "source/common/ssl/tls_certificate_config_impl.h"

namespace Envoy {
namespace Secret {

SecretManagerImpl::SecretManagerImpl(OptRef<Server::ConfigTracker> config_tracker) {
  if (config_tracker.has_value()) {
    config_tracker_entry_ =
        config_tracker->add("secrets", [this](const Matchers::StringMatcher& name_matcher) {
          return dumpSecretConfigs(name_matcher);
        });
  }
}

void SecretManagerImpl::addStaticSecret(
    const envoy::extensions::transport_sockets::tls::v3::Secret& secret) {
  switch (secret.type_case()) {
  case envoy::extensions::transport_sockets::tls::v3::Secret::TypeCase::kTlsCertificate: {
    auto secret_provider =
        std::make_shared<TlsCertificateConfigProviderImpl>(secret.tls_certificate());
    if (!static_tls_certificate_providers_.insert(std::make_pair(secret.name(), secret_provider))
             .second) {
      throw EnvoyException(
          absl::StrCat("Duplicate static TlsCertificate secret name ", secret.name()));
    }
    break;
  }
  case envoy::extensions::transport_sockets::tls::v3::Secret::TypeCase::kValidationContext: {
    auto secret_provider = std::make_shared<CertificateValidationContextConfigProviderImpl>(
        secret.validation_context());
    if (!static_certificate_validation_context_providers_
             .insert(std::make_pair(secret.name(), secret_provider))
             .second) {
      throw EnvoyException(absl::StrCat(
          "Duplicate static CertificateValidationContext secret name ", secret.name()));
    }
    break;
  }
  case envoy::extensions::transport_sockets::tls::v3::Secret::TypeCase::kSessionTicketKeys: {
    auto secret_provider =
        std::make_shared<TlsSessionTicketKeysConfigProviderImpl>(secret.session_ticket_keys());
    if (!static_session_ticket_keys_providers_
             .insert(std::make_pair(secret.name(), secret_provider))
             .second) {
      throw EnvoyException(
          absl::StrCat("Duplicate static TlsSessionTicketKeys secret name ", secret.name()));
    }
    break;
  }
  case envoy::extensions::transport_sockets::tls::v3::Secret::TypeCase::kGenericSecret: {
    auto secret_provider =
        std::make_shared<GenericSecretConfigProviderImpl>(secret.generic_secret());
    if (!static_generic_secret_providers_.insert(std::make_pair(secret.name(), secret_provider))
             .second) {
      throw EnvoyException(
          absl::StrCat("Duplicate static GenericSecret secret name ", secret.name()));
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

TlsSessionTicketKeysConfigProviderSharedPtr
SecretManagerImpl::findStaticTlsSessionTicketKeysContextProvider(const std::string& name) const {
  auto secret = static_session_ticket_keys_providers_.find(name);
  return (secret != static_session_ticket_keys_providers_.end()) ? secret->second : nullptr;
}

GenericSecretConfigProviderSharedPtr
SecretManagerImpl::findStaticGenericSecretProvider(const std::string& name) const {
  auto secret = static_generic_secret_providers_.find(name);
  return (secret != static_generic_secret_providers_.end()) ? secret->second : nullptr;
}

TlsCertificateConfigProviderSharedPtr SecretManagerImpl::createInlineTlsCertificateProvider(
    const envoy::extensions::transport_sockets::tls::v3::TlsCertificate& tls_certificate) {
  return std::make_shared<TlsCertificateConfigProviderImpl>(tls_certificate);
}

CertificateValidationContextConfigProviderSharedPtr
SecretManagerImpl::createInlineCertificateValidationContextProvider(
    const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext&
        certificate_validation_context) {
  return std::make_shared<CertificateValidationContextConfigProviderImpl>(
      certificate_validation_context);
}

TlsSessionTicketKeysConfigProviderSharedPtr
SecretManagerImpl::createInlineTlsSessionTicketKeysProvider(
    const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys&
        tls_session_ticket_keys) {
  return std::make_shared<TlsSessionTicketKeysConfigProviderImpl>(tls_session_ticket_keys);
}

GenericSecretConfigProviderSharedPtr SecretManagerImpl::createInlineGenericSecretProvider(
    const envoy::extensions::transport_sockets::tls::v3::GenericSecret& generic_secret) {
  return std::make_shared<GenericSecretConfigProviderImpl>(generic_secret);
}

TlsCertificateConfigProviderSharedPtr SecretManagerImpl::findOrCreateTlsCertificateProvider(
    const envoy::config::core::v3::ConfigSource& sds_config_source, const std::string& config_name,
    Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
    Init::Manager& init_manager) {
  return certificate_providers_.findOrCreate(sds_config_source, config_name,
                                             secret_provider_context, init_manager);
}

CertificateValidationContextConfigProviderSharedPtr
SecretManagerImpl::findOrCreateCertificateValidationContextProvider(
    const envoy::config::core::v3::ConfigSource& sds_config_source, const std::string& config_name,
    Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
    Init::Manager& init_manager) {
  return validation_context_providers_.findOrCreate(sds_config_source, config_name,
                                                    secret_provider_context, init_manager);
}

TlsSessionTicketKeysConfigProviderSharedPtr
SecretManagerImpl::findOrCreateTlsSessionTicketKeysContextProvider(
    const envoy::config::core::v3::ConfigSource& sds_config_source, const std::string& config_name,
    Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
    Init::Manager& init_manager) {
  return session_ticket_keys_providers_.findOrCreate(sds_config_source, config_name,
                                                     secret_provider_context, init_manager);
}

GenericSecretConfigProviderSharedPtr SecretManagerImpl::findOrCreateGenericSecretProvider(
    const envoy::config::core::v3::ConfigSource& sds_config_source, const std::string& config_name,
    Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
    Init::Manager& init_manager) {
  return generic_secret_providers_.findOrCreate(sds_config_source, config_name,
                                                secret_provider_context, init_manager);
}

ProtobufTypes::MessagePtr
SecretManagerImpl::dumpSecretConfigs(const Matchers::StringMatcher& name_matcher) {
  auto config_dump = std::make_unique<envoy::admin::v3::SecretsConfigDump>();
  // Handle static tls key/cert providers.
  for (const auto& cert_iter : static_tls_certificate_providers_) {
    const auto& tls_cert = cert_iter.second;
    ASSERT(tls_cert != nullptr);
    envoy::extensions::transport_sockets::tls::v3::Secret dump_secret;
    dump_secret.set_name(cert_iter.first);
    dump_secret.mutable_tls_certificate()->MergeFrom(*tls_cert->secret());
    if (!name_matcher.match(dump_secret.name())) {
      continue;
    }
    MessageUtil::redact(dump_secret);
    auto static_secret = config_dump->mutable_static_secrets()->Add();
    static_secret->set_name(cert_iter.first);
    static_secret->mutable_secret()->PackFrom(dump_secret);
  }

  // Handle static certificate validation context providers.
  for (const auto& context_iter : static_certificate_validation_context_providers_) {
    const auto& validation_context = context_iter.second;
    ASSERT(validation_context != nullptr);
    envoy::extensions::transport_sockets::tls::v3::Secret dump_secret;
    dump_secret.set_name(context_iter.first);
    dump_secret.mutable_validation_context()->MergeFrom(*validation_context->secret());
    if (!name_matcher.match(dump_secret.name())) {
      continue;
    }
    auto static_secret = config_dump->mutable_static_secrets()->Add();
    static_secret->set_name(context_iter.first);
    static_secret->mutable_secret()->PackFrom(dump_secret);
  }

  // Handle static session keys providers.
  for (const auto& context_iter : static_session_ticket_keys_providers_) {
    const auto& session_ticket_keys = context_iter.second;
    ASSERT(session_ticket_keys != nullptr);
    envoy::extensions::transport_sockets::tls::v3::Secret dump_secret;
    dump_secret.set_name(context_iter.first);
    for (const auto& key : session_ticket_keys->secret()->keys()) {
      dump_secret.mutable_session_ticket_keys()->add_keys()->MergeFrom(key);
    }
    if (!name_matcher.match(dump_secret.name())) {
      continue;
    }
    MessageUtil::redact(dump_secret);
    auto static_secret = config_dump->mutable_static_secrets()->Add();
    static_secret->set_name(context_iter.first);
    static_secret->mutable_secret()->PackFrom(dump_secret);
  }

  // Handle static generic secret providers.
  for (const auto& secret_iter : static_generic_secret_providers_) {
    const auto& generic_secret = secret_iter.second;
    ASSERT(generic_secret != nullptr);
    envoy::extensions::transport_sockets::tls::v3::Secret dump_secret;
    dump_secret.set_name(secret_iter.first);
    dump_secret.mutable_generic_secret()->MergeFrom(*generic_secret->secret());
    if (!name_matcher.match(dump_secret.name())) {
      continue;
    }
    auto static_secret = config_dump->mutable_static_secrets()->Add();
    static_secret->set_name(secret_iter.first);
    MessageUtil::redact(dump_secret);
    static_secret->mutable_secret()->PackFrom(dump_secret);
  }

  // Handle dynamic tls_certificate providers.
  const auto providers = certificate_providers_.allSecretProviders();
  for (const auto& cert_secrets : providers) {
    const auto& secret_data = cert_secrets->secretData();
    const auto& tls_cert = cert_secrets->secret();
    const bool secret_ready = tls_cert != nullptr;
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(secret_data.resource_name_);
    ProtobufWkt::Timestamp last_updated_ts;
    TimestampUtil::systemClockToTimestamp(secret_data.last_updated_, last_updated_ts);
    secret.set_name(secret_data.resource_name_);
    if (secret_ready) {
      secret.mutable_tls_certificate()->MergeFrom(*tls_cert);
    }
    if (!name_matcher.match(secret.name())) {
      continue;
    }
    MessageUtil::redact(secret);
    envoy::admin::v3::SecretsConfigDump::DynamicSecret* dump_secret;
    if (secret_ready) {
      dump_secret = config_dump->mutable_dynamic_active_secrets()->Add();
    } else {
      dump_secret = config_dump->mutable_dynamic_warming_secrets()->Add();
    }
    dump_secret->set_name(secret_data.resource_name_);
    dump_secret->set_version_info(secret_data.version_info_);
    *dump_secret->mutable_last_updated() = last_updated_ts;
    dump_secret->mutable_secret()->PackFrom(secret);
  }

  // Handling dynamic cert validation context providers.
  const auto context_secret_provider = validation_context_providers_.allSecretProviders();
  for (const auto& validation_context_secret : context_secret_provider) {
    const auto& secret_data = validation_context_secret->secretData();
    const auto& validation_context = validation_context_secret->secret();
    const bool secret_ready = validation_context != nullptr;

    envoy::extensions::transport_sockets::tls::v3::Secret secret;

    if (secret_ready) {
      secret.mutable_validation_context()->MergeFrom(*validation_context);
    }
    secret.set_name(secret_data.resource_name_);
    if (!name_matcher.match(secret.name())) {
      continue;
    }
    ProtobufWkt::Timestamp last_updated_ts;
    envoy::admin::v3::SecretsConfigDump::DynamicSecret* dump_secret;
    TimestampUtil::systemClockToTimestamp(secret_data.last_updated_, last_updated_ts);
    if (secret_ready) {
      dump_secret = config_dump->mutable_dynamic_active_secrets()->Add();
    } else {
      dump_secret = config_dump->mutable_dynamic_warming_secrets()->Add();
    }
    dump_secret->set_version_info(secret_data.version_info_);
    *dump_secret->mutable_last_updated() = last_updated_ts;
    dump_secret->set_name(secret_data.resource_name_);
    dump_secret->mutable_secret()->PackFrom(secret);
  }

  // Handle dynamic session keys providers providers.
  const auto stek_providers = session_ticket_keys_providers_.allSecretProviders();
  for (const auto& stek_secrets : stek_providers) {
    const auto& secret_data = stek_secrets->secretData();
    const auto& tls_stek = stek_secrets->secret();
    const bool secret_ready = tls_stek != nullptr;
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(secret_data.resource_name_);
    if (secret_ready) {
      secret.mutable_session_ticket_keys()->MergeFrom(*tls_stek);
    }
    if (!name_matcher.match(secret.name())) {
      continue;
    }
    ProtobufWkt::Timestamp last_updated_ts;
    TimestampUtil::systemClockToTimestamp(secret_data.last_updated_, last_updated_ts);
    envoy::admin::v3::SecretsConfigDump::DynamicSecret* dump_secret;
    if (secret_ready) {
      dump_secret = config_dump->mutable_dynamic_active_secrets()->Add();
    } else {
      dump_secret = config_dump->mutable_dynamic_warming_secrets()->Add();
    }
    dump_secret->set_name(secret_data.resource_name_);
    dump_secret->set_version_info(secret_data.version_info_);
    *dump_secret->mutable_last_updated() = last_updated_ts;
    MessageUtil::redact(secret);
    dump_secret->mutable_secret()->PackFrom(secret);
  }

  // Handle dynamic generic secret providers.
  const auto generic_secret_providers = generic_secret_providers_.allSecretProviders();
  for (const auto& provider : generic_secret_providers) {
    const auto& secret_data = provider->secretData();
    const auto& generic_secret = provider->secret();
    const bool secret_ready = generic_secret != nullptr;
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(secret_data.resource_name_);
    if (secret_ready) {
      secret.mutable_generic_secret()->MergeFrom(*generic_secret);
    }
    if (!name_matcher.match(secret.name())) {
      continue;
    }
    ProtobufWkt::Timestamp last_updated_ts;
    TimestampUtil::systemClockToTimestamp(secret_data.last_updated_, last_updated_ts);
    envoy::admin::v3::SecretsConfigDump::DynamicSecret* dump_secret;
    if (secret_ready) {
      dump_secret = config_dump->mutable_dynamic_active_secrets()->Add();
    } else {
      dump_secret = config_dump->mutable_dynamic_warming_secrets()->Add();
    }
    dump_secret->set_name(secret_data.resource_name_);
    dump_secret->set_version_info(secret_data.version_info_);
    *dump_secret->mutable_last_updated() = last_updated_ts;
    MessageUtil::redact(secret);
    dump_secret->mutable_secret()->PackFrom(secret);
  }

  return config_dump;
}

} // namespace Secret
} // namespace Envoy
