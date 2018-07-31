#include "common/ssl/context_config_impl.h"

#include <memory>
#include <string>

#include "envoy/ssl/tls_certificate_config.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/config/datasource.h"
#include "common/config/tls_context_json.h"
#include "common/protobuf/utility.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Ssl {

const std::string ContextConfigImpl::DEFAULT_CIPHER_SUITES =
    "[ECDHE-ECDSA-AES128-GCM-SHA256|ECDHE-ECDSA-CHACHA20-POLY1305]:"
    "[ECDHE-RSA-AES128-GCM-SHA256|ECDHE-RSA-CHACHA20-POLY1305]:"
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

const std::string ContextConfigImpl::DEFAULT_ECDH_CURVES = "X25519:P-256";

ContextConfigImpl::ContextConfigImpl(
    const envoy::api::v2::auth::CommonTlsContext& config, Secret::SecretManager& secret_manager,
    Secret::DynamicTlsCertificateSecretProviderFactory& secret_provider_factory)
    : secret_manager_(secret_manager),
      alpn_protocols_(RepeatedPtrUtil::join(config.alpn_protocols(), ",")),
      alt_alpn_protocols_(config.deprecated_v1().alt_alpn_protocols()),
      cipher_suites_(StringUtil::nonEmptyStringOrDefault(
          RepeatedPtrUtil::join(config.tls_params().cipher_suites(), ":"), DEFAULT_CIPHER_SUITES)),
      ecdh_curves_(StringUtil::nonEmptyStringOrDefault(
          RepeatedPtrUtil::join(config.tls_params().ecdh_curves(), ":"), DEFAULT_ECDH_CURVES)),
      ca_cert_(Config::DataSource::read(config.validation_context().trusted_ca(), true)),
      ca_cert_path_(Config::DataSource::getPath(config.validation_context().trusted_ca())),
      certificate_revocation_list_(
          Config::DataSource::read(config.validation_context().crl(), true)),
      certificate_revocation_list_path_(
          Config::DataSource::getPath(config.validation_context().crl())),
      cert_chain_path_(
          config.tls_certificates().empty()
              ? ""
              : Config::DataSource::getPath(config.tls_certificates()[0].certificate_chain())),
      private_key_path_(
          config.tls_certificates().empty()
              ? ""
              : Config::DataSource::getPath(config.tls_certificates()[0].private_key())),
      verify_subject_alt_name_list_(config.validation_context().verify_subject_alt_name().begin(),
                                    config.validation_context().verify_subject_alt_name().end()),
      verify_certificate_hash_list_(config.validation_context().verify_certificate_hash().begin(),
                                    config.validation_context().verify_certificate_hash().end()),
      verify_certificate_spki_list_(config.validation_context().verify_certificate_spki().begin(),
                                    config.validation_context().verify_certificate_spki().end()),
      allow_expired_certificate_(config.validation_context().allow_expired_certificate()),
      min_protocol_version_(
          tlsVersionFromProto(config.tls_params().tls_minimum_protocol_version(), TLS1_VERSION)),
      max_protocol_version_(
          tlsVersionFromProto(config.tls_params().tls_maximum_protocol_version(), TLS1_2_VERSION)) {

  ENVOY_LOG(info, "Received ca_cert (ca_cert_: {})", ca_cert_);
  readCertChainConfig(config, secret_provider_factory);

  if (ca_cert_.empty()) {
    if (!certificate_revocation_list_.empty()) {
      throw EnvoyException(fmt::format("Failed to load CRL from {} without trusted CA",
                                       certificateRevocationListPath()));
    }
    if (!verify_subject_alt_name_list_.empty()) {
      throw EnvoyException(fmt::format("SAN-based verification of peer certificates without "
                                       "trusted CA is insecure and not allowed"));
    }
    if (allow_expired_certificate_) {
      throw EnvoyException(
          fmt::format("Certificate validity period is always ignored without trusted CA"));
    }
  }
}

void ContextConfigImpl::readCertChainConfig(
    const envoy::api::v2::auth::CommonTlsContext& config,
    Secret::DynamicTlsCertificateSecretProviderFactory& secret_provider_factory) {
  if (!config.tls_certificates().empty()) {
    cert_chain_ = Config::DataSource::read(config.tls_certificates()[0].certificate_chain(), true);
    private_key_ = Config::DataSource::read(config.tls_certificates()[0].private_key(), true);
    return;
  }
  if (!config.tls_certificate_sds_secret_configs().empty()) {
    auto secret_name = config.tls_certificate_sds_secret_configs()[0].name();
    if (!config.tls_certificate_sds_secret_configs()[0].has_sds_config()) {
      // static secret
      const auto secret = secret_manager_.findStaticTlsCertificate(secret_name);
      if (secret) {
        cert_chain_ = secret->certificateChain();
        private_key_ = secret->privateKey();
        return;
      } else {
        throw EnvoyException(fmt::format("Unknown static secret: {}", secret_name));
      }
    } else {
      secret_provider_ = secret_provider_factory.findOrCreate(
          config.tls_certificate_sds_secret_configs()[0].sds_config(), secret_name);
      return;
    }
  }
}

unsigned ContextConfigImpl::tlsVersionFromProto(
    const envoy::api::v2::auth::TlsParameters_TlsProtocol& version, unsigned default_version) {
  switch (version) {
  case envoy::api::v2::auth::TlsParameters::TLS_AUTO:
    return default_version;
  case envoy::api::v2::auth::TlsParameters::TLSv1_0:
    return TLS1_VERSION;
  case envoy::api::v2::auth::TlsParameters::TLSv1_1:
    return TLS1_1_VERSION;
  case envoy::api::v2::auth::TlsParameters::TLSv1_2:
    return TLS1_2_VERSION;
  case envoy::api::v2::auth::TlsParameters::TLSv1_3:
    return TLS1_3_VERSION;
  default:
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

const std::string& ContextConfigImpl::certChain() const {
  if (secret_provider_ && secret_provider_->secret()) {
    return secret_provider_->secret()->certificateChain();
  }

  return cert_chain_;
}

const std::string& ContextConfigImpl::privateKey() const {
  if (secret_provider_ && secret_provider_->secret()) {
    return secret_provider_->secret()->privateKey();
  }

  return private_key_;
}

ClientContextConfigImpl::ClientContextConfigImpl(
    const envoy::api::v2::auth::UpstreamTlsContext& config, Secret::SecretManager& secret_manager,
    Secret::DynamicTlsCertificateSecretProviderFactory& secret_provider_factory)
    : ContextConfigImpl(config.common_tls_context(), secret_manager, secret_provider_factory),
      server_name_indication_(config.sni()), allow_renegotiation_(config.allow_renegotiation()) {
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

ClientContextConfigImpl::ClientContextConfigImpl(
    const Json::Object& config, Secret::SecretManager& secret_manager,
    Secret::DynamicTlsCertificateSecretProviderFactory& secret_provider_factory)
    : ClientContextConfigImpl(
          [&config] {
            envoy::api::v2::auth::UpstreamTlsContext upstream_tls_context;
            Config::TlsContextJson::translateUpstreamTlsContext(config, upstream_tls_context);
            return upstream_tls_context;
          }(),
          secret_manager, secret_provider_factory) {}

ServerContextConfigImpl::ServerContextConfigImpl(
    const envoy::api::v2::auth::DownstreamTlsContext& config, Secret::SecretManager& secret_manager,
    Secret::DynamicTlsCertificateSecretProviderFactory& secret_provider_factory)
    : ContextConfigImpl(config.common_tls_context(), secret_manager, secret_provider_factory),
      require_client_certificate_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, require_client_certificate, false)),
      session_ticket_keys_([&config] {
        std::vector<SessionTicketKey> ret;

        switch (config.session_ticket_keys_type_case()) {
        case envoy::api::v2::auth::DownstreamTlsContext::kSessionTicketKeys:
          for (const auto& datasource : config.session_ticket_keys().keys()) {
            validateAndAppendKey(ret, Config::DataSource::read(datasource, false));
          }
          break;
        case envoy::api::v2::auth::DownstreamTlsContext::kSessionTicketKeysSdsSecretConfig:
          throw EnvoyException("SDS not supported yet");
          break;
        case envoy::api::v2::auth::DownstreamTlsContext::SESSION_TICKET_KEYS_TYPE_NOT_SET:
          break;
        default:
          throw EnvoyException(fmt::format("Unexpected case for oneof session_ticket_keys: {}",
                                           config.session_ticket_keys_type_case()));
        }

        return ret;
      }()) {
  // TODO(PiotrSikora): Support multiple TLS certificates.
  if ((config.common_tls_context().tls_certificates().size() +
       config.common_tls_context().tls_certificate_sds_secret_configs().size()) != 1) {
    throw EnvoyException("A single TLS certificate is required for server contexts");
  }
}

ServerContextConfigImpl::ServerContextConfigImpl(
    const Json::Object& config, Secret::SecretManager& secret_manager,
    Secret::DynamicTlsCertificateSecretProviderFactory& secret_provider_factory)
    : ServerContextConfigImpl(
          [&config] {
            envoy::api::v2::auth::DownstreamTlsContext downstream_tls_context;
            Config::TlsContextJson::translateDownstreamTlsContext(config, downstream_tls_context);
            return downstream_tls_context;
          }(),
          secret_manager, secret_provider_factory) {}

// Append a SessionTicketKey to keys, initializing it with key_data.
// Throws if key_data is invalid.
void ServerContextConfigImpl::validateAndAppendKey(
    std::vector<ServerContextConfig::SessionTicketKey>& keys, const std::string& key_data) {
  // If this changes, need to figure out how to deal with key files
  // that previously worked. For now, just assert so we'll notice that
  // it changed if it does.
  static_assert(sizeof(SessionTicketKey) == 80, "Input is expected to be this size");

  if (key_data.size() != sizeof(SessionTicketKey)) {
    throw EnvoyException(fmt::format("Incorrect TLS session ticket key length. "
                                     "Length {}, expected length {}.",
                                     key_data.size(), sizeof(SessionTicketKey)));
  }

  keys.emplace_back();
  SessionTicketKey& dst_key = keys.back();

  std::copy_n(key_data.begin(), dst_key.name_.size(), dst_key.name_.begin());
  size_t pos = dst_key.name_.size();
  std::copy_n(key_data.begin() + pos, dst_key.hmac_key_.size(), dst_key.hmac_key_.begin());
  pos += dst_key.hmac_key_.size();
  std::copy_n(key_data.begin() + pos, dst_key.aes_key_.size(), dst_key.aes_key_.begin());
  pos += dst_key.aes_key_.size();
  ASSERT(key_data.begin() + pos == key_data.end());
}

} // namespace Ssl
} // namespace Envoy
