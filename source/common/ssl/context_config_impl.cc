#include "common/ssl/context_config_impl.h"

#include <string>

#include "common/common/assert.h"
#include "common/config/tls_context_json.h"
#include "common/filesystem/filesystem_impl.h"
#include "common/protobuf/utility.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Ssl {

const std::string ContextConfigImpl::DEFAULT_CIPHER_SUITES =
    "[ECDHE-ECDSA-AES128-GCM-SHA256|ECDHE-ECDSA-CHACHA20-POLY1305]:"
    "[ECDHE-RSA-AES128-GCM-SHA256|ECDHE-RSA-CHACHA20-POLY1305]:"
    "ECDHE-ECDSA-AES128-SHA256:"
    "ECDHE-RSA-AES128-SHA256:"
    "ECDHE-ECDSA-AES128-SHA:"
    "ECDHE-RSA-AES128-SHA:"
    "AES128-GCM-SHA256:"
    "AES128-SHA256:"
    "AES128-SHA:"
    "ECDHE-ECDSA-AES256-GCM-SHA384:"
    "ECDHE-RSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-AES256-SHA384:"
    "ECDHE-RSA-AES256-SHA384:"
    "ECDHE-ECDSA-AES256-SHA:"
    "ECDHE-RSA-AES256-SHA:"
    "AES256-GCM-SHA384:"
    "AES256-SHA256:"
    "AES256-SHA";

const std::string ContextConfigImpl::DEFAULT_ECDH_CURVES = "X25519:P-256";

ContextConfigImpl::ContextConfigImpl(const envoy::api::v2::auth::CommonTlsContext& config)
    : alpn_protocols_(RepeatedPtrUtil::join(config.alpn_protocols(), ",")),
      alt_alpn_protocols_(config.deprecated_v1().alt_alpn_protocols()),
      cipher_suites_(StringUtil::nonEmptyStringOrDefault(
          RepeatedPtrUtil::join(config.tls_params().cipher_suites(), ":"), DEFAULT_CIPHER_SUITES)),
      ecdh_curves_(StringUtil::nonEmptyStringOrDefault(
          RepeatedPtrUtil::join(config.tls_params().ecdh_curves(), ":"), DEFAULT_ECDH_CURVES)),
      ca_cert_(readDataSource(config.validation_context().trusted_ca(), true)),
      ca_cert_path_(getDataSourcePath(config.validation_context().trusted_ca())),
      certificate_revocation_list_(readDataSource(config.validation_context().crl(), true)),
      certificate_revocation_list_path_(getDataSourcePath(config.validation_context().crl())),
      cert_chain_(config.tls_certificates().empty()
                      ? ""
                      : readDataSource(config.tls_certificates()[0].certificate_chain(), true)),
      cert_chain_path_(config.tls_certificates().empty()
                           ? ""
                           : getDataSourcePath(config.tls_certificates()[0].certificate_chain())),
      private_key_(config.tls_certificates().empty()
                       ? ""
                       : readDataSource(config.tls_certificates()[0].private_key(), true)),
      private_key_path_(config.tls_certificates().empty()
                            ? ""
                            : getDataSourcePath(config.tls_certificates()[0].private_key())),
      verify_subject_alt_name_list_(config.validation_context().verify_subject_alt_name().begin(),
                                    config.validation_context().verify_subject_alt_name().end()),
      verify_certificate_hash_(config.validation_context().verify_certificate_hash().empty()
                                   ? ""
                                   : config.validation_context().verify_certificate_hash()[0]),
      min_protocol_version_(
          tlsVersionFromProto(config.tls_params().tls_minimum_protocol_version(), TLS1_VERSION)),
      max_protocol_version_(
          tlsVersionFromProto(config.tls_params().tls_maximum_protocol_version(), TLS1_2_VERSION)) {
  // TODO(htuch): Support multiple hashes.
  ASSERT(config.validation_context().verify_certificate_hash().size() <= 1);
  if (ca_cert_.empty() && !certificate_revocation_list_.empty()) {
    throw EnvoyException(fmt::format("Failed to load CRL from {} without trusted CA certificates",
                                     certificateRevocationListPath()));
  }
}

const std::string ContextConfigImpl::readDataSource(const envoy::api::v2::core::DataSource& source,
                                                    bool allow_empty) {
  switch (source.specifier_case()) {
  case envoy::api::v2::core::DataSource::kFilename:
    return Filesystem::fileReadToEnd(source.filename());
  case envoy::api::v2::core::DataSource::kInlineBytes:
    return source.inline_bytes();
  case envoy::api::v2::core::DataSource::kInlineString:
    return source.inline_string();
  default:
    if (!allow_empty) {
      throw EnvoyException(
          fmt::format("Unexpected DataSource::specifier_case(): {}", source.specifier_case()));
    }
    return "";
  }
}

const std::string
ContextConfigImpl::getDataSourcePath(const envoy::api::v2::core::DataSource& source) {
  return source.specifier_case() == envoy::api::v2::core::DataSource::kFilename ? source.filename()
                                                                                : "";
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
    NOT_IMPLEMENTED;
  }

  NOT_REACHED;
}

ClientContextConfigImpl::ClientContextConfigImpl(
    const envoy::api::v2::auth::UpstreamTlsContext& config)
    : ContextConfigImpl(config.common_tls_context()), server_name_indication_(config.sni()) {
  // TODO(PiotrSikora): Support multiple TLS certificates.
  ASSERT(config.common_tls_context().tls_certificates().size() <= 1);
}

ClientContextConfigImpl::ClientContextConfigImpl(const Json::Object& config)
    : ClientContextConfigImpl([&config] {
        envoy::api::v2::auth::UpstreamTlsContext upstream_tls_context;
        Config::TlsContextJson::translateUpstreamTlsContext(config, upstream_tls_context);
        return upstream_tls_context;
      }()) {}

ServerContextConfigImpl::ServerContextConfigImpl(
    const envoy::api::v2::auth::DownstreamTlsContext& config)
    : ContextConfigImpl(config.common_tls_context()),
      require_client_certificate_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, require_client_certificate, false)),
      session_ticket_keys_([&config] {
        std::vector<SessionTicketKey> ret;

        switch (config.session_ticket_keys_type_case()) {
        case envoy::api::v2::auth::DownstreamTlsContext::kSessionTicketKeys:
          for (const auto& datasource : config.session_ticket_keys().keys()) {
            validateAndAppendKey(ret, readDataSource(datasource, false));
          }
          break;
        case envoy::api::v2::auth::DownstreamTlsContext::kSessionTicketKeysSdsSecretConfig:
          NOT_IMPLEMENTED;
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
  // TODO(mattklein123): All of the ASSERTs in this file need to be converted to exceptions with
  //                     proper error handling.
  ASSERT(config.common_tls_context().tls_certificates().size() == 1);
}

ServerContextConfigImpl::ServerContextConfigImpl(const Json::Object& config)
    : ServerContextConfigImpl([&config] {
        envoy::api::v2::auth::DownstreamTlsContext downstream_tls_context;
        Config::TlsContextJson::translateDownstreamTlsContext(config, downstream_tls_context);
        return downstream_tls_context;
      }()) {}

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
