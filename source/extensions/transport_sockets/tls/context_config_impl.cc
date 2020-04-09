#include "extensions/transport_sockets/tls/context_config_impl.h"

#include <memory>
#include <string>

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/config/datasource.h"
#include "common/protobuf/utility.h"
#include "common/secret/sds_api.h"
#include "common/ssl/certificate_validation_context_config_impl.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

namespace {

std::vector<Secret::TlsCertificateConfigProviderSharedPtr> getTlsCertificateConfigProviders(
    const envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context) {
  if (!config.tls_certificates().empty()) {
    std::vector<Secret::TlsCertificateConfigProviderSharedPtr> providers;
    for (const auto& tls_certificate : config.tls_certificates()) {
      if (!tls_certificate.has_private_key_provider() && !tls_certificate.has_certificate_chain() &&
          !tls_certificate.has_private_key()) {
        continue;
      }
      providers.push_back(
          factory_context.secretManager().createInlineTlsCertificateProvider(tls_certificate));
    }
    return providers;
  }
  if (!config.tls_certificate_sds_secret_configs().empty()) {
    const auto& sds_secret_config = config.tls_certificate_sds_secret_configs(0);
    if (sds_secret_config.has_sds_config()) {
      // Fetch dynamic secret.
      return {factory_context.secretManager().findOrCreateTlsCertificateProvider(
          sds_secret_config.sds_config(), sds_secret_config.name(), factory_context)};
    } else {
      // Load static secret.
      auto secret_provider = factory_context.secretManager().findStaticTlsCertificateProvider(
          sds_secret_config.name());
      if (!secret_provider) {
        throw EnvoyException(fmt::format("Unknown static secret: {}", sds_secret_config.name()));
      }
      return {secret_provider};
    }
  }
  return {};
}

Secret::CertificateValidationContextConfigProviderSharedPtr getProviderFromSds(
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    const envoy::extensions::transport_sockets::tls::v3::SdsSecretConfig& sds_secret_config) {
  if (sds_secret_config.has_sds_config()) {
    // Fetch dynamic secret.
    return factory_context.secretManager().findOrCreateCertificateValidationContextProvider(
        sds_secret_config.sds_config(), sds_secret_config.name(), factory_context);
  } else {
    // Load static secret.
    auto secret_provider =
        factory_context.secretManager().findStaticCertificateValidationContextProvider(
            sds_secret_config.name());
    if (!secret_provider) {
      throw EnvoyException(fmt::format("Unknown static certificate validation context: {}",
                                       sds_secret_config.name()));
    }
    return secret_provider;
  }
  return nullptr;
}

Secret::CertificateValidationContextConfigProviderSharedPtr
getCertificateValidationContextConfigProvider(
    const envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    std::unique_ptr<envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext>*
        default_cvc) {
  switch (config.validation_context_type_case()) {
  case envoy::extensions::transport_sockets::tls::v3::CommonTlsContext::ValidationContextTypeCase::
      kValidationContext: {
    auto secret_provider =
        factory_context.secretManager().createInlineCertificateValidationContextProvider(
            config.validation_context());
    return secret_provider;
  }
  case envoy::extensions::transport_sockets::tls::v3::CommonTlsContext::ValidationContextTypeCase::
      kValidationContextSdsSecretConfig: {
    const auto& sds_secret_config = config.validation_context_sds_secret_config();
    return getProviderFromSds(factory_context, sds_secret_config);
  }
  case envoy::extensions::transport_sockets::tls::v3::CommonTlsContext::ValidationContextTypeCase::
      kCombinedValidationContext: {
    *default_cvc = std::make_unique<
        envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext>(
        config.combined_validation_context().default_validation_context());
    const auto& sds_secret_config =
        config.combined_validation_context().validation_context_sds_secret_config();
    return getProviderFromSds(factory_context, sds_secret_config);
  }
  default:
    return nullptr;
  }
}

Secret::TlsSessionTicketKeysConfigProviderSharedPtr getTlsSessionTicketKeysConfigProvider(
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    const envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext& config) {

  switch (config.session_ticket_keys_type_case()) {
  case envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext::
      SessionTicketKeysTypeCase::kSessionTicketKeys:
    return factory_context.secretManager().createInlineTlsSessionTicketKeysProvider(
        config.session_ticket_keys());
  case envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext::
      SessionTicketKeysTypeCase::kSessionTicketKeysSdsSecretConfig: {
    const auto& sds_secret_config = config.session_ticket_keys_sds_secret_config();
    if (sds_secret_config.has_sds_config()) {
      // Fetch dynamic secret.
      return factory_context.secretManager().findOrCreateTlsSessionTicketKeysContextProvider(
          sds_secret_config.sds_config(), sds_secret_config.name(), factory_context);
    } else {
      // Load static secret.
      auto secret_provider =
          factory_context.secretManager().findStaticTlsSessionTicketKeysContextProvider(
              sds_secret_config.name());
      if (!secret_provider) {
        throw EnvoyException(
            fmt::format("Unknown tls session ticket keys: {}", sds_secret_config.name()));
      }
      return secret_provider;
    }
  }
  case envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext::
      SessionTicketKeysTypeCase::kDisableStatelessSessionResumption:
  case envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext::
      SessionTicketKeysTypeCase::SESSION_TICKET_KEYS_TYPE_NOT_SET:
    return nullptr;
  default:
    throw EnvoyException(fmt::format("Unexpected case for oneof session_ticket_keys: {}",
                                     config.session_ticket_keys_type_case()));
  }
}

bool getStatelessSessionResumptionDisabled(
    const envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext& config) {
  if (config.session_ticket_keys_type_case() ==
      envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext::
          SessionTicketKeysTypeCase::kDisableStatelessSessionResumption) {
    return config.disable_stateless_session_resumption();
  } else {
    return false;
  }
}

} // namespace

ContextConfigImpl::ContextConfigImpl(
    const envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& config,
    const unsigned default_min_protocol_version, const unsigned default_max_protocol_version,
    const std::string& default_cipher_suites, const std::string& default_curves,
    Server::Configuration::TransportSocketFactoryContext& factory_context)
    : api_(factory_context.api()),
      alpn_protocols_(RepeatedPtrUtil::join(config.alpn_protocols(), ",")),
      cipher_suites_(StringUtil::nonEmptyStringOrDefault(
          RepeatedPtrUtil::join(config.tls_params().cipher_suites(), ":"), default_cipher_suites)),
      ecdh_curves_(StringUtil::nonEmptyStringOrDefault(
          RepeatedPtrUtil::join(config.tls_params().ecdh_curves(), ":"), default_curves)),
      tls_certificate_providers_(getTlsCertificateConfigProviders(config, factory_context)),
      certificate_validation_context_provider_(
          getCertificateValidationContextConfigProvider(config, factory_context, &default_cvc_)),
      min_protocol_version_(tlsVersionFromProto(config.tls_params().tls_minimum_protocol_version(),
                                                default_min_protocol_version)),
      max_protocol_version_(tlsVersionFromProto(config.tls_params().tls_maximum_protocol_version(),
                                                default_max_protocol_version)) {
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
                      dynamic_cvc) { getCombinedValidationContextConfig(dynamic_cvc); });
    }
    // Load inlined, static or dynamic secret that's already available.
    if (certificate_validation_context_provider_->secret() != nullptr) {
      if (default_cvc_) {
        validation_context_config_ =
            getCombinedValidationContextConfig(*certificate_validation_context_provider_->secret());
      } else {
        validation_context_config_ = std::make_unique<Ssl::CertificateValidationContextConfigImpl>(
            *certificate_validation_context_provider_->secret(), api_);
      }
    }
  }
  // Load inlined, static or dynamic secrets that are already available.
  if (!tls_certificate_providers_.empty()) {
    for (auto& provider : tls_certificate_providers_) {
      if (provider->secret() != nullptr) {
        tls_certificate_configs_.emplace_back(*provider->secret(), &factory_context, api_);
      }
    }
  }
}

Ssl::CertificateValidationContextConfigPtr ContextConfigImpl::getCombinedValidationContextConfig(
    const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext&
        dynamic_cvc) {
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext combined_cvc =
      *default_cvc_;
  combined_cvc.MergeFrom(dynamic_cvc);
  return std::make_unique<Envoy::Ssl::CertificateValidationContextConfigImpl>(combined_cvc, api_);
}

void ContextConfigImpl::setSecretUpdateCallback(std::function<void()> callback) {
  if (!tls_certificate_providers_.empty()) {
    if (tc_update_callback_handle_) {
      tc_update_callback_handle_->remove();
    }
    // Once tls_certificate_config_ receives new secret, this callback updates
    // ContextConfigImpl::tls_certificate_config_ with new secret.
    tc_update_callback_handle_ =
        tls_certificate_providers_[0]->addUpdateCallback([this, callback]() {
          // This breaks multiple certificate support, but today SDS is only single cert.
          // TODO(htuch): Fix this when SDS goes multi-cert.
          tls_certificate_configs_.clear();
          tls_certificate_configs_.emplace_back(*tls_certificate_providers_[0]->secret(), nullptr,
                                                api_);
          callback();
        });
  }
  if (certificate_validation_context_provider_) {
    if (cvc_update_callback_handle_) {
      cvc_update_callback_handle_->remove();
    }
    if (default_cvc_) {
      // Once certificate_validation_context_provider_ receives new secret, this callback updates
      // ContextConfigImpl::validation_context_config_ with a combined certificate validation
      // context. The combined certificate validation context is created by merging new secret into
      // default_cvc_.
      cvc_update_callback_handle_ =
          certificate_validation_context_provider_->addUpdateCallback([this, callback]() {
            validation_context_config_ = getCombinedValidationContextConfig(
                *certificate_validation_context_provider_->secret());
            callback();
          });
    } else {
      // Once certificate_validation_context_provider_ receives new secret, this callback updates
      // ContextConfigImpl::validation_context_config_ with new secret.
      cvc_update_callback_handle_ =
          certificate_validation_context_provider_->addUpdateCallback([this, callback]() {
            validation_context_config_ =
                std::make_unique<Ssl::CertificateValidationContextConfigImpl>(
                    *certificate_validation_context_provider_->secret(), api_);
            callback();
          });
    }
  }
}

ContextConfigImpl::~ContextConfigImpl() {
  if (tc_update_callback_handle_) {
    tc_update_callback_handle_->remove();
  }
  if (cvc_update_callback_handle_) {
    cvc_update_callback_handle_->remove();
  }
  if (cvc_validation_callback_handle_) {
    cvc_validation_callback_handle_->remove();
  }
}

unsigned ContextConfigImpl::tlsVersionFromProto(
    const envoy::extensions::transport_sockets::tls::v3::TlsParameters::TlsProtocol& version,
    unsigned default_version) {
  switch (version) {
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
  default:
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
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
    "ECDHE-ECDSA-AES128-SHA:"
    "ECDHE-RSA-AES128-SHA:"
    "AES128-GCM-SHA256:"
    "AES128-SHA:"
    "ECDHE-ECDSA-AES256-GCM-SHA384:"
    "ECDHE-RSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-AES256-SHA:"
    "ECDHE-RSA-AES256-SHA:"
    "AES256-GCM-SHA384:"
    "AES256-SHA";

const std::string ClientContextConfigImpl::DEFAULT_CURVES =
#ifndef BORINGSSL_FIPS
    "X25519:"
#endif
    "P-256";

ClientContextConfigImpl::ClientContextConfigImpl(
    const envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext& config,
    absl::string_view sigalgs,
    Server::Configuration::TransportSocketFactoryContext& factory_context)
    : ContextConfigImpl(config.common_tls_context(), DEFAULT_MIN_VERSION, DEFAULT_MAX_VERSION,
                        DEFAULT_CIPHER_SUITES, DEFAULT_CURVES, factory_context),
      server_name_indication_(config.sni()), allow_renegotiation_(config.allow_renegotiation()),
      max_session_keys_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_session_keys, 1)),
      sigalgs_(sigalgs) {
  // BoringSSL treats this as a C string, so embedded NULL characters will not
  // be handled correctly.
  if (server_name_indication_.find('\0') != std::string::npos) {
    throw EnvoyException("SNI names containing NULL-byte are not allowed");
  }
  // TODO(PiotrSikora): Support multiple TLS certificates.
  if ((config.common_tls_context().tls_certificates().size() +
       config.common_tls_context().tls_certificate_sds_secret_configs().size()) > 1) {
    throw EnvoyException("Multiple TLS certificates are not supported for client contexts");
  }
}

const unsigned ServerContextConfigImpl::DEFAULT_MIN_VERSION = TLS1_VERSION;
const unsigned ServerContextConfigImpl::DEFAULT_MAX_VERSION =
#ifndef BORINGSSL_FIPS
    TLS1_3_VERSION;
#else // BoringSSL FIPS
    TLS1_2_VERSION;
#endif

const std::string ServerContextConfigImpl::DEFAULT_CIPHER_SUITES =
#ifndef BORINGSSL_FIPS
    "[ECDHE-ECDSA-AES128-GCM-SHA256|ECDHE-ECDSA-CHACHA20-POLY1305]:"
    "[ECDHE-RSA-AES128-GCM-SHA256|ECDHE-RSA-CHACHA20-POLY1305]:"
#else // BoringSSL FIPS
    "ECDHE-ECDSA-AES128-GCM-SHA256:"
    "ECDHE-RSA-AES128-GCM-SHA256:"
#endif
    "ECDHE-ECDSA-AES128-SHA:"
    "ECDHE-RSA-AES128-SHA:"
    "AES128-GCM-SHA256:"
    "AES128-SHA:"
    "ECDHE-ECDSA-AES256-GCM-SHA384:"
    "ECDHE-RSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-AES256-SHA:"
    "ECDHE-RSA-AES256-SHA:"
    "AES256-GCM-SHA384:"
    "AES256-SHA";

const std::string ServerContextConfigImpl::DEFAULT_CURVES =
#ifndef BORINGSSL_FIPS
    "X25519:"
#endif
    "P-256";

ServerContextConfigImpl::ServerContextConfigImpl(
    const envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context)
    : ContextConfigImpl(config.common_tls_context(), DEFAULT_MIN_VERSION, DEFAULT_MAX_VERSION,
                        DEFAULT_CIPHER_SUITES, DEFAULT_CURVES, factory_context),
      require_client_certificate_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, require_client_certificate, false)),
      session_ticket_keys_provider_(getTlsSessionTicketKeysConfigProvider(factory_context, config)),
      disable_stateless_session_resumption_(getStatelessSessionResumptionDisabled(config)) {

  if (session_ticket_keys_provider_ != nullptr) {
    // Validate tls session ticket keys early to reject bad sds updates.
    stk_validation_callback_handle_ = session_ticket_keys_provider_->addValidationCallback(
        [this](const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys& keys) {
          getSessionTicketKeys(keys);
        });
    // Load inlined, static or dynamic secret that's already available.
    if (session_ticket_keys_provider_->secret() != nullptr) {
      session_ticket_keys_ = getSessionTicketKeys(*session_ticket_keys_provider_->secret());
    }
  }

  if ((config.common_tls_context().tls_certificates().size() +
       config.common_tls_context().tls_certificate_sds_secret_configs().size()) == 0) {
    throw EnvoyException("No TLS certificates found for server context");
  } else if (!config.common_tls_context().tls_certificates().empty() &&
             !config.common_tls_context().tls_certificate_sds_secret_configs().empty()) {
    throw EnvoyException("SDS and non-SDS TLS certificates may not be mixed in server contexts");
  }

  if (config.has_session_timeout()) {
    session_timeout_ =
        std::chrono::seconds(DurationUtil::durationToSeconds(config.session_timeout()));
  }
}

ServerContextConfigImpl::~ServerContextConfigImpl() {
  if (stk_update_callback_handle_ != nullptr) {
    stk_update_callback_handle_->remove();
  }
  if (stk_validation_callback_handle_ != nullptr) {
    stk_validation_callback_handle_->remove();
  }
}

void ServerContextConfigImpl::setSecretUpdateCallback(std::function<void()> callback) {
  ContextConfigImpl::setSecretUpdateCallback(callback);
  if (session_ticket_keys_provider_) {
    if (stk_update_callback_handle_) {
      stk_update_callback_handle_->remove();
    }
    // Once session_ticket_keys_ receives new secret, this callback updates
    // ContextConfigImpl::session_ticket_keys_ with new session ticket keys.
    stk_update_callback_handle_ =
        session_ticket_keys_provider_->addUpdateCallback([this, callback]() {
          session_ticket_keys_ = getSessionTicketKeys(*session_ticket_keys_provider_->secret());
          callback();
        });
  }
}

std::vector<Ssl::ServerContextConfig::SessionTicketKey>
ServerContextConfigImpl::getSessionTicketKeys(
    const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys& keys) {
  std::vector<Ssl::ServerContextConfig::SessionTicketKey> result;
  for (const auto& datasource : keys.keys()) {
    result.emplace_back(getSessionTicketKey(Config::DataSource::read(datasource, false, api_)));
  }
  return result;
}

// Extracts a SessionTicketKey from raw binary data.
// Throws if key_data is invalid.
Ssl::ServerContextConfig::SessionTicketKey
ServerContextConfigImpl::getSessionTicketKey(const std::string& key_data) {
  // If this changes, need to figure out how to deal with key files
  // that previously worked. For now, just assert so we'll notice that
  // it changed if it does.
  static_assert(sizeof(SessionTicketKey) == 80, "Input is expected to be this size");

  if (key_data.size() != sizeof(SessionTicketKey)) {
    throw EnvoyException(fmt::format("Incorrect TLS session ticket key length. "
                                     "Length {}, expected length {}.",
                                     key_data.size(), sizeof(SessionTicketKey)));
  }

  SessionTicketKey dst_key;

  std::copy_n(key_data.begin(), dst_key.name_.size(), dst_key.name_.begin());
  size_t pos = dst_key.name_.size();
  std::copy_n(key_data.begin() + pos, dst_key.hmac_key_.size(), dst_key.hmac_key_.begin());
  pos += dst_key.hmac_key_.size();
  std::copy_n(key_data.begin() + pos, dst_key.aes_key_.size(), dst_key.aes_key_.begin());
  pos += dst_key.aes_key_.size();
  ASSERT(key_data.begin() + pos == key_data.end());

  return dst_key;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
