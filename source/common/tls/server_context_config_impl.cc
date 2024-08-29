#include "source/common/tls/server_context_config_impl.h"

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
#include "source/common/tls/default_tls_certificate_selector.h"
#include "source/common/tls/ssl_handshaker.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

namespace {

Secret::TlsSessionTicketKeysConfigProviderSharedPtr getTlsSessionTicketKeysConfigProvider(
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    const envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext& config,
    absl::Status& creation_status) {
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
          sds_secret_config.sds_config(), sds_secret_config.name(), factory_context,
          factory_context.initManager());
    } else {
      // Load static secret.
      auto secret_provider =
          factory_context.secretManager().findStaticTlsSessionTicketKeysContextProvider(
              sds_secret_config.name());
      if (secret_provider) {
        return secret_provider;
      }
      creation_status = absl::InvalidArgumentError(
          fmt::format("Unknown tls session ticket keys: {}", sds_secret_config.name()));
      return nullptr;
    }
  }
  case envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext::
      SessionTicketKeysTypeCase::kDisableStatelessSessionResumption:
  case envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext::
      SessionTicketKeysTypeCase::SESSION_TICKET_KEYS_TYPE_NOT_SET:
    return nullptr;
  default:
    creation_status = absl::InvalidArgumentError(
        fmt::format("Unexpected case for oneof session_ticket_keys: {}",
                    static_cast<int>(config.session_ticket_keys_type_case())));
    return nullptr;
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

const unsigned ServerContextConfigImpl::DEFAULT_MIN_VERSION = TLS1_2_VERSION;
const unsigned ServerContextConfigImpl::DEFAULT_MAX_VERSION = TLS1_3_VERSION;

const std::string ServerContextConfigImpl::DEFAULT_CIPHER_SUITES =
#ifndef BORINGSSL_FIPS
    "[ECDHE-ECDSA-AES128-GCM-SHA256|ECDHE-ECDSA-CHACHA20-POLY1305]:"
    "[ECDHE-RSA-AES128-GCM-SHA256|ECDHE-RSA-CHACHA20-POLY1305]:"
#else // BoringSSL FIPS
    "ECDHE-ECDSA-AES128-GCM-SHA256:"
    "ECDHE-RSA-AES128-GCM-SHA256:"
#endif
    "ECDHE-ECDSA-AES256-GCM-SHA384:"
    "ECDHE-RSA-AES256-GCM-SHA384:";

const std::string ServerContextConfigImpl::DEFAULT_CURVES =
#ifndef BORINGSSL_FIPS
    "X25519:"
#endif
    "P-256";

absl::StatusOr<std::unique_ptr<ServerContextConfigImpl>> ServerContextConfigImpl::create(
    const envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext& config,
    Server::Configuration::TransportSocketFactoryContext& secret_provider_context, bool for_quic) {
  absl::Status creation_status = absl::OkStatus();
  std::unique_ptr<ServerContextConfigImpl> ret = absl::WrapUnique(
      new ServerContextConfigImpl(config, secret_provider_context, creation_status, for_quic));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

ServerContextConfigImpl::ServerContextConfigImpl(
    const envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    absl::Status& creation_status, bool for_quic)
    : ContextConfigImpl(config.common_tls_context(), DEFAULT_MIN_VERSION, DEFAULT_MAX_VERSION,
                        DEFAULT_CIPHER_SUITES, DEFAULT_CURVES, factory_context, creation_status),
      require_client_certificate_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, require_client_certificate, false)),
      ocsp_staple_policy_(ocspStaplePolicyFromProto(config.ocsp_staple_policy())),
      session_ticket_keys_provider_(
          getTlsSessionTicketKeysConfigProvider(factory_context, config, creation_status)),
      disable_stateless_session_resumption_(getStatelessSessionResumptionDisabled(config)),
      disable_stateful_session_resumption_(config.disable_stateful_session_resumption()),
      full_scan_certs_on_sni_mismatch_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, full_scan_certs_on_sni_mismatch, false)),
      prefer_client_ciphers_(config.prefer_client_ciphers()) {
  SET_AND_RETURN_IF_NOT_OK(creation_status, creation_status);
  if (session_ticket_keys_provider_ != nullptr) {
    // Validate tls session ticket keys early to reject bad sds updates.
    stk_validation_callback_handle_ = session_ticket_keys_provider_->addValidationCallback(
        [this](const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys& keys) {
          return getSessionTicketKeys(keys).status();
        });
    // Load inlined, static or dynamic secret that's already available.
    if (session_ticket_keys_provider_->secret() != nullptr) {
      auto keys_or_error = getSessionTicketKeys(*session_ticket_keys_provider_->secret());
      SET_AND_RETURN_IF_NOT_OK(keys_or_error.status(), creation_status);
      session_ticket_keys_ = *keys_or_error;
    }
  }

  if (!capabilities().provides_certificates) {
    if ((config.common_tls_context().tls_certificates().size() +
         config.common_tls_context().tls_certificate_sds_secret_configs().size()) == 0) {
      creation_status = absl::InvalidArgumentError("No TLS certificates found for server context");
    } else if (!config.common_tls_context().tls_certificates().empty() &&
               !config.common_tls_context().tls_certificate_sds_secret_configs().empty()) {
      creation_status = absl::InvalidArgumentError(
          "SDS and non-SDS TLS certificates may not be mixed in server contexts");
      return;
    }
  }

  if (config.has_session_timeout()) {
    session_timeout_ =
        std::chrono::seconds(DurationUtil::durationToSeconds(config.session_timeout()));
  }

  if (config.common_tls_context().has_custom_tls_certificate_selector()) {
    // If a custom tls context provider is configured, derive the factory from the config.
    const auto& provider_config = config.common_tls_context().custom_tls_certificate_selector();
    Ssl::TlsCertificateSelectorConfigFactory* provider_factory =
        &Config::Utility::getAndCheckFactory<Ssl::TlsCertificateSelectorConfigFactory>(
            provider_config);
    tls_certificate_selector_factory_ = provider_factory->createTlsCertificateSelectorFactory(
        provider_config.typed_config(), factory_context.serverFactoryContext(),
        factory_context.messageValidationVisitor(), creation_status, for_quic);
    return;
  }

  auto factory =
      TlsCertificateSelectorConfigFactoryImpl::getDefaultTlsCertificateSelectorConfigFactory();
  const ProtobufWkt::Any any;
  tls_certificate_selector_factory_ = factory->createTlsCertificateSelectorFactory(
      any, factory_context.serverFactoryContext(), ProtobufMessage::getNullValidationVisitor(),
      creation_status, for_quic);
}

void ServerContextConfigImpl::setSecretUpdateCallback(std::function<absl::Status()> callback) {
  ContextConfigImpl::setSecretUpdateCallback(callback);
  if (session_ticket_keys_provider_) {
    // Once session_ticket_keys_ receives new secret, this callback updates
    // ContextConfigImpl::session_ticket_keys_ with new session ticket keys.
    stk_update_callback_handle_ =
        session_ticket_keys_provider_->addUpdateCallback([this, callback]() {
          auto keys_or_error = getSessionTicketKeys(*session_ticket_keys_provider_->secret());
          RETURN_IF_NOT_OK(keys_or_error.status());
          session_ticket_keys_ = *keys_or_error;
          return callback();
        });
  }
}

absl::StatusOr<std::vector<Ssl::ServerContextConfig::SessionTicketKey>>
ServerContextConfigImpl::getSessionTicketKeys(
    const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys& keys) {
  std::vector<Ssl::ServerContextConfig::SessionTicketKey> result;
  for (const auto& datasource : keys.keys()) {
    auto datasource_or_error = Config::DataSource::read(datasource, false, api_);
    RETURN_IF_NOT_OK(datasource_or_error.status());
    auto key_or_error = getSessionTicketKey(std::move(*datasource_or_error));
    RETURN_IF_NOT_OK(key_or_error.status());
    result.emplace_back(std::move(*key_or_error));
  }
  return result;
}

// Extracts a SessionTicketKey from raw binary data.
// Throws if key_data is invalid.
absl::StatusOr<Ssl::ServerContextConfig::SessionTicketKey>
ServerContextConfigImpl::getSessionTicketKey(const std::string& key_data) {
  // If this changes, need to figure out how to deal with key files
  // that previously worked. For now, just assert so we'll notice that
  // it changed if it does.
  static_assert(sizeof(SessionTicketKey) == 80, "Input is expected to be this size");

  if (key_data.size() != sizeof(SessionTicketKey)) {
    return absl::InvalidArgumentError(fmt::format("Incorrect TLS session ticket key length. "
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

Ssl::ServerContextConfig::OcspStaplePolicy ServerContextConfigImpl::ocspStaplePolicyFromProto(
    const envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext::OcspStaplePolicy&
        policy) {
  switch (policy) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext::LENIENT_STAPLING:
    return Ssl::ServerContextConfig::OcspStaplePolicy::LenientStapling;
  case envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext::STRICT_STAPLING:
    return Ssl::ServerContextConfig::OcspStaplePolicy::StrictStapling;
  case envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext::MUST_STAPLE:
    return Ssl::ServerContextConfig::OcspStaplePolicy::MustStaple;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

Ssl::TlsCertificateSelectorFactory ServerContextConfigImpl::tlsCertificateSelectorFactory() const {
  if (!tls_certificate_selector_factory_) {
    IS_ENVOY_BUG("No envoy.tls.certificate_selectors registered");
  }
  return tls_certificate_selector_factory_;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
