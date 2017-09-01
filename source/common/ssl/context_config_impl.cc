#include "common/ssl/context_config_impl.h"

#include <string>

#include "common/common/assert.h"
#include "common/config/tls_context_json.h"
#include "common/protobuf/utility.h"

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

ContextConfigImpl::ContextConfigImpl(const envoy::api::v2::CommonTlsContext& config)
    : alpn_protocols_(RepeatedPtrUtil::join(config.alpn_protocols(), ",")),
      alt_alpn_protocols_(config.deprecated_v1().alt_alpn_protocols()),
      cipher_suites_(StringUtil::nonEmptyStringOrDefault(
          RepeatedPtrUtil::join(config.tls_params().cipher_suites(), ":"), DEFAULT_CIPHER_SUITES)),
      ecdh_curves_(StringUtil::nonEmptyStringOrDefault(
          RepeatedPtrUtil::join(config.tls_params().ecdh_curves(), ":"), DEFAULT_ECDH_CURVES)),
      ca_cert_file_(config.validation_context().trusted_ca().filename()),
      cert_chain_file_(config.tls_certificates()[0].certificate_chain().filename()),
      private_key_file_(config.tls_certificates()[0].private_key().filename()),
      verify_subject_alt_name_list_(config.validation_context().verify_subject_alt_name().begin(),
                                    config.validation_context().verify_subject_alt_name().end()),
      verify_certificate_hash_(config.validation_context().verify_certificate_hash().empty()
                                   ? ""
                                   : config.validation_context().verify_certificate_hash()[0]) {
  // TODO(htuch): Support multiple hashes.
  ASSERT(config.validation_context().verify_certificate_hash().size() <= 1);
  // TODO(PiotrSikora): Support multiple TLS certificates.
  ASSERT(config.tls_certificates().size() == 1);
  // TODO(htuch): Support inline cert material delivery.
  ASSERT(config.tls_certificates()[0].certificate_chain().specifier_case() ==
         envoy::api::v2::DataSource::kFilename);
  ASSERT(config.tls_certificates()[0].private_key().specifier_case() ==
         envoy::api::v2::DataSource::kFilename);
}

ClientContextConfigImpl::ClientContextConfigImpl(const envoy::api::v2::UpstreamTlsContext& config)
    : ContextConfigImpl(config.common_tls_context()), server_name_indication_(config.sni()) {}

ClientContextConfigImpl::ClientContextConfigImpl(const Json::Object& config)
    : ClientContextConfigImpl([&config] {
        envoy::api::v2::UpstreamTlsContext upstream_tls_context;
        Config::TlsContextJson::translateUpstreamTlsContext(config, upstream_tls_context);
        return upstream_tls_context;
      }()) {}

ServerContextConfigImpl::ServerContextConfigImpl(const envoy::api::v2::DownstreamTlsContext& config)
    : ContextConfigImpl(config.common_tls_context()),
      require_client_certificate_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, require_client_certificate, false)) {}

ServerContextConfigImpl::ServerContextConfigImpl(const Json::Object& config)
    : ServerContextConfigImpl([&config] {
        envoy::api::v2::DownstreamTlsContext downstream_tls_context;
        Config::TlsContextJson::translateDownstreamTlsContext(config, downstream_tls_context);
        return downstream_tls_context;
      }()) {}

} // namespace Ssl
} // namespace Envoy
