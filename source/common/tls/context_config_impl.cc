#include "source/common/tls/context_config_impl.h"

#include <memory>
#include <string>

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/config/datasource.h"
#include "source/common/network/cidr_range.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/secret/sds_api.h"
#include "source/common/ssl/certificate_validation_context_config_impl.h"
#include "source/common/tls/ssl_handshaker.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

namespace {

std::vector<Secret::TlsCertificateConfigProviderSharedPtr> getTlsCertificateConfigProviders(
    const envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    absl::Status& creation_status) {
  std::vector<Secret::TlsCertificateConfigProviderSharedPtr> providers;
  if (!config.tls_certificates().empty()) {
    for (const auto& tls_certificate : config.tls_certificates()) {
      if (!tls_certificate.has_private_key_provider() && !tls_certificate.has_certificate_chain() &&
          !tls_certificate.has_private_key() && !tls_certificate.has_pkcs12()) {
        continue;
      }
      providers.push_back(
          factory_context.secretManager().createInlineTlsCertificateProvider(tls_certificate));
    }
    return providers;
  }
  if (!config.tls_certificate_sds_secret_configs().empty()) {
    for (const auto& sds_secret_config : config.tls_certificate_sds_secret_configs()) {
      if (sds_secret_config.has_sds_config()) {
        // Fetch dynamic secret.
        providers.push_back(factory_context.secretManager().findOrCreateTlsCertificateProvider(
            sds_secret_config.sds_config(), sds_secret_config.name(), factory_context,
            factory_context.initManager()));
      } else {
        // Load static secret.
        auto secret_provider = factory_context.secretManager().findStaticTlsCertificateProvider(
            sds_secret_config.name());
        if (!secret_provider) {
          creation_status = absl::InvalidArgumentError(
              fmt::format("Unknown static secret: {}", sds_secret_config.name()));
          return {};
        }
        providers.push_back(secret_provider);
      }
    }
    return providers;
  }
  return {};
}

Secret::CertificateValidationContextConfigProviderSharedPtr getProviderFromSds(
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    const envoy::extensions::transport_sockets::tls::v3::SdsSecretConfig& sds_secret_config,
    absl::Status& creation_status) {
  if (sds_secret_config.has_sds_config()) {
    // Fetch dynamic secret.
    return factory_context.secretManager().findOrCreateCertificateValidationContextProvider(
        sds_secret_config.sds_config(), sds_secret_config.name(), factory_context,
        factory_context.initManager());
  } else {
    // Load static secret.
    auto secret_provider =
        factory_context.secretManager().findStaticCertificateValidationContextProvider(
            sds_secret_config.name());
    if (secret_provider) {
      return secret_provider;
    }
    creation_status = absl::InvalidArgumentError(
        fmt::format("Unknown static certificate validation context: {}", sds_secret_config.name()));
  }
  return nullptr;
}

Secret::CertificateValidationContextConfigProviderSharedPtr
getCertificateValidationContextConfigProvider(
    const envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    std::unique_ptr<envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext>*
        default_cvc,
    absl::Status& creation_status) {
  switch (config.validation_context_type_case()) {
  case envoy::extensions::transport_sockets::tls::v3::CommonTlsContext::ValidationContextTypeCase::
      kValidationContext:
    return factory_context.secretManager().createInlineCertificateValidationContextProvider(
        config.validation_context());
  case envoy::extensions::transport_sockets::tls::v3::CommonTlsContext::ValidationContextTypeCase::
      kValidationContextSdsSecretConfig:
    return getProviderFromSds(factory_context, config.validation_context_sds_secret_config(),
                              creation_status);
  case envoy::extensions::transport_sockets::tls::v3::CommonTlsContext::ValidationContextTypeCase::
      kCombinedValidationContext: {
    *default_cvc = std::make_unique<
        envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext>(
        config.combined_validation_context().default_validation_context());
    const auto& sds_secret_config =
        config.combined_validation_context().validation_context_sds_secret_config();
    return getProviderFromSds(factory_context, sds_secret_config, creation_status);
  }
  default:
    return nullptr;
  }
}

} // namespace

ContextConfigImpl::ContextConfigImpl(
    const envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& config,
    const unsigned default_min_protocol_version, const unsigned default_max_protocol_version,
    const std::string& default_cipher_suites, const std::string& default_curves,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    absl::Status& creation_status)
    : api_(factory_context.serverFactoryContext().api()),
      options_(factory_context.serverFactoryContext().options()),
      singleton_manager_(factory_context.serverFactoryContext().singletonManager()),
      lifecycle_notifier_(factory_context.serverFactoryContext().lifecycleNotifier()),
      alpn_protocols_(RepeatedPtrUtil::join(config.alpn_protocols(), ",")),
      cipher_suites_(StringUtil::nonEmptyStringOrDefault(
          RepeatedPtrUtil::join(config.tls_params().cipher_suites(), ":"), default_cipher_suites)),
      ecdh_curves_(StringUtil::nonEmptyStringOrDefault(
          RepeatedPtrUtil::join(config.tls_params().ecdh_curves(), ":"), default_curves)),
      signature_algorithms_(RepeatedPtrUtil::join(config.tls_params().signature_algorithms(), ":")),
      tls_certificate_providers_(
          getTlsCertificateConfigProviders(config, factory_context, creation_status)),
      certificate_validation_context_provider_(getCertificateValidationContextConfigProvider(
          config, factory_context, &default_cvc_, creation_status)),
      min_protocol_version_(tlsVersionFromProto(config.tls_params().tls_minimum_protocol_version(),
                                                default_min_protocol_version)),
      max_protocol_version_(tlsVersionFromProto(config.tls_params().tls_maximum_protocol_version(),
                                                default_max_protocol_version)),
      factory_context_(factory_context), tls_keylog_path_(config.key_log().path()) {
  SET_AND_RETURN_IF_NOT_OK(creation_status, creation_status);
  auto list_or_error = Network::Address::IpList::create(config.key_log().local_address_range());
  SET_AND_RETURN_IF_NOT_OK(list_or_error.status(), creation_status);
  tls_keylog_local_ = std::move(list_or_error.value());
  list_or_error = Network::Address::IpList::create(config.key_log().remote_address_range());
  SET_AND_RETURN_IF_NOT_OK(list_or_error.status(), creation_status);
  tls_keylog_remote_ = std::move(list_or_error.value());

  if (certificate_validation_context_provider_ != nullptr) {
    if (default_cvc_) {
      // We need to validate combined certificate validation context.
      // The default certificate validation context and dynamic certificate validation
      // context could only contain partial fields, which is okay to fail the validation.
      // But the combined certificate validation context should pass validation. If
      // validation of combined certificate validation context fails,
      // getCombinedValidationContextConfig() throws exception, validation_context_config_ will not
      // get updated.
      cvc_validation_callback_handle_ =
          certificate_validation_context_provider_->addValidationCallback(
              [this](
                  const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext&
                      dynamic_cvc) {
                return getCombinedValidationContextConfig(dynamic_cvc).status();
              });
    }
    // Load inlined, static or dynamic secret that's already available.
    if (certificate_validation_context_provider_->secret() != nullptr) {
      if (default_cvc_) {
        auto context_or_error =
            getCombinedValidationContextConfig(*certificate_validation_context_provider_->secret());
        SET_AND_RETURN_IF_NOT_OK(context_or_error.status(), creation_status);
        validation_context_config_ = std::move(*context_or_error);
      } else {
        auto config_or_status = Envoy::Ssl::CertificateValidationContextConfigImpl::create(
            *certificate_validation_context_provider_->secret(), api_);
        SET_AND_RETURN_IF_NOT_OK(config_or_status.status(), creation_status);
        validation_context_config_ = std::move(config_or_status.value());
      }
    }
  }
  // Load inlined, static or dynamic secrets that are already available.
  if (!tls_certificate_providers_.empty()) {
    for (auto& provider : tls_certificate_providers_) {
      if (provider->secret() != nullptr) {
        auto config_or_error =
            Ssl::TlsCertificateConfigImpl::create(*provider->secret(), factory_context, api_);
        SET_AND_RETURN_IF_NOT_OK(config_or_error.status(), creation_status);
        tls_certificate_configs_.emplace_back(std::move(*config_or_error));
      }
    }
  }

  HandshakerFactoryContextImpl handshaker_factory_context(api_, options_, alpn_protocols_,
                                                          singleton_manager_, lifecycle_notifier_);
  Ssl::HandshakerFactory* handshaker_factory;
  if (config.has_custom_handshaker()) {
    // If a custom handshaker is configured, derive the factory from the config.
    const auto& handshaker_config = config.custom_handshaker();
    handshaker_factory =
        &Config::Utility::getAndCheckFactory<Ssl::HandshakerFactory>(handshaker_config);
    handshaker_factory_cb_ = handshaker_factory->createHandshakerCb(
        handshaker_config.typed_config(), handshaker_factory_context,
        factory_context.messageValidationVisitor());
  } else {
    // Otherwise, derive the config from the default factory.
    handshaker_factory = HandshakerFactoryImpl::getDefaultHandshakerFactory();
    handshaker_factory_cb_ = handshaker_factory->createHandshakerCb(
        *handshaker_factory->createEmptyConfigProto(), handshaker_factory_context,
        factory_context.messageValidationVisitor());
  }
  capabilities_ = handshaker_factory->capabilities();
  sslctx_cb_ = handshaker_factory->sslctxCb(handshaker_factory_context);
}

absl::StatusOr<Ssl::CertificateValidationContextConfigPtr>
ContextConfigImpl::getCombinedValidationContextConfig(
    const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext&
        dynamic_cvc) {
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext combined_cvc =
      *default_cvc_;
  combined_cvc.MergeFrom(dynamic_cvc);
  auto config_or_status =
      Envoy::Ssl::CertificateValidationContextConfigImpl::create(combined_cvc, api_);
  RETURN_IF_NOT_OK(config_or_status.status());
  return std::move(config_or_status.value());
}

void ContextConfigImpl::setSecretUpdateCallback(std::function<absl::Status()> callback) {
  // When any of tls_certificate_providers_ receives a new secret, this callback updates
  // ContextConfigImpl::tls_certificate_configs_ with new secret.
  for (const auto& tls_certificate_provider : tls_certificate_providers_) {
    tc_update_callback_handles_.push_back(
        tls_certificate_provider->addUpdateCallback([this, callback]() {
          tls_certificate_configs_.clear();
          for (const auto& tls_certificate_provider : tls_certificate_providers_) {
            auto* secret = tls_certificate_provider->secret();
            if (secret != nullptr) {
              auto config_or_error =
                  Ssl::TlsCertificateConfigImpl::create(*secret, factory_context_, api_);
              RETURN_IF_NOT_OK(config_or_error.status());
              tls_certificate_configs_.emplace_back(std::move(*config_or_error));
            }
          }
          return callback();
        }));
  }
  if (certificate_validation_context_provider_) {
    if (default_cvc_) {
      // Once certificate_validation_context_provider_ receives new secret, this callback updates
      // ContextConfigImpl::validation_context_config_ with a combined certificate validation
      // context. The combined certificate validation context is created by merging new secret
      // into default_cvc_.
      cvc_update_callback_handle_ =
          certificate_validation_context_provider_->addUpdateCallback([this, callback]() {
            auto context_or_error = getCombinedValidationContextConfig(
                *certificate_validation_context_provider_->secret());
            RETURN_IF_NOT_OK(context_or_error.status());
            validation_context_config_ = std::move(*context_or_error);
            return callback();
          });
    } else {
      // Once certificate_validation_context_provider_ receives new secret, this callback updates
      // ContextConfigImpl::validation_context_config_ with new secret.
      cvc_update_callback_handle_ =
          certificate_validation_context_provider_->addUpdateCallback([this, callback]() {
            auto config_or_status = Envoy::Ssl::CertificateValidationContextConfigImpl::create(
                *certificate_validation_context_provider_->secret(), api_);
            RETURN_IF_NOT_OK(config_or_status.status());
            validation_context_config_ = std::move(config_or_status.value());
            return callback();
          });
    }
  }
}

Ssl::HandshakerFactoryCb ContextConfigImpl::createHandshaker() const {
  return handshaker_factory_cb_;
}

unsigned ContextConfigImpl::tlsVersionFromProto(
    const envoy::extensions::transport_sockets::tls::v3::TlsParameters::TlsProtocol& version,
    unsigned default_version) {
  switch (version) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLS_AUTO:
    return default_version;
  case envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_0:
    return TLS1_VERSION;
  case envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_1:
    return TLS1_1_VERSION;
  case envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2:
    return TLS1_2_VERSION;
  case envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3:
    return TLS1_3_VERSION;
  }
  IS_ENVOY_BUG("unexpected tls version provided");
  return default_version;
}

const unsigned ClientContextConfigImpl::DEFAULT_MIN_VERSION = TLS1_2_VERSION;
const unsigned ClientContextConfigImpl::DEFAULT_MAX_VERSION = TLS1_2_VERSION;

const std::string ClientContextConfigImpl::DEFAULT_CIPHER_SUITES =
#ifndef BORINGSSL_FIPS
    "[ECDHE-ECDSA-AES128-GCM-SHA256|ECDHE-ECDSA-CHACHA20-POLY1305]:"
    "[ECDHE-RSA-AES128-GCM-SHA256|ECDHE-RSA-CHACHA20-POLY1305]:"
#else // BoringSSL FIPS
    "ECDHE-ECDSA-AES128-GCM-SHA256:"
    "ECDHE-RSA-AES128-GCM-SHA256:"
#endif
    "ECDHE-ECDSA-AES256-GCM-SHA384:"
    "ECDHE-RSA-AES256-GCM-SHA384:";

const std::string ClientContextConfigImpl::DEFAULT_CURVES =
#ifndef BORINGSSL_FIPS
    "X25519:"
#endif
    "P-256";

absl::StatusOr<std::unique_ptr<ClientContextConfigImpl>> ClientContextConfigImpl::create(
    const envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext& config,
    Server::Configuration::TransportSocketFactoryContext& secret_provider_context) {
  absl::Status creation_status = absl::OkStatus();
  std::unique_ptr<ClientContextConfigImpl> ret = absl::WrapUnique(
      new ClientContextConfigImpl(config, secret_provider_context, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

ClientContextConfigImpl::ClientContextConfigImpl(
    const envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    absl::Status& creation_status)
    : ContextConfigImpl(config.common_tls_context(), DEFAULT_MIN_VERSION, DEFAULT_MAX_VERSION,
                        DEFAULT_CIPHER_SUITES, DEFAULT_CURVES, factory_context, creation_status),
      server_name_indication_(config.sni()), allow_renegotiation_(config.allow_renegotiation()),
      enforce_rsa_key_usage_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, enforce_rsa_key_usage, false)),
      max_session_keys_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_session_keys, 1)) {
  // BoringSSL treats this as a C string, so embedded NULL characters will not
  // be handled correctly.
  if (server_name_indication_.find('\0') != std::string::npos) {
    creation_status = absl::InvalidArgumentError("SNI names containing NULL-byte are not allowed");
    return;
  }
  // TODO(PiotrSikora): Support multiple TLS certificates.
  if ((config.common_tls_context().tls_certificates().size() +
       config.common_tls_context().tls_certificate_sds_secret_configs().size()) > 1) {
    creation_status = absl::InvalidArgumentError(
        "Multiple TLS certificates are not supported for client contexts");
    return;
  }
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
