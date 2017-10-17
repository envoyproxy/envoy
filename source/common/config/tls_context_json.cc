#include "common/config/tls_context_json.h"

#include "common/common/utility.h"
#include "common/config/json_utility.h"

namespace Envoy {
namespace Config {

void TlsContextJson::translateDownstreamTlsContext(
    const Json::Object& json_tls_context,
    envoy::api::v2::DownstreamTlsContext& downstream_tls_context) {
  translateCommonTlsContext(json_tls_context, *downstream_tls_context.mutable_common_tls_context());
  JSON_UTIL_SET_BOOL(json_tls_context, downstream_tls_context, require_client_certificate);

  const std::vector<std::string> paths =
      json_tls_context.getStringArray("session_ticket_key_paths", true);
  for (const std::string& path : paths) {
    downstream_tls_context.mutable_session_ticket_keys()->mutable_keys()->Add()->set_filename(path);
  }
}

void TlsContextJson::translateUpstreamTlsContext(
    const Json::Object& json_tls_context,
    envoy::api::v2::UpstreamTlsContext& upstream_tls_context) {
  translateCommonTlsContext(json_tls_context, *upstream_tls_context.mutable_common_tls_context());
  upstream_tls_context.set_sni(json_tls_context.getString("sni", ""));
}

void TlsContextJson::translateCommonTlsContext(
    const Json::Object& json_tls_context, envoy::api::v2::CommonTlsContext& common_tls_context) {
  const std::vector<std::string> alpn_protocols =
      StringUtil::split(json_tls_context.getString("alpn_protocols", ""), ",");
  for (const auto& alpn_protocol : alpn_protocols) {
    common_tls_context.add_alpn_protocols(alpn_protocol);
  }

  common_tls_context.mutable_deprecated_v1()->set_alt_alpn_protocols(
      json_tls_context.getString("alt_alpn_protocols", ""));

  translateTlsCertificate(json_tls_context, *common_tls_context.mutable_tls_certificates()->Add());

  auto* validation_context = common_tls_context.mutable_validation_context();
  validation_context->mutable_trusted_ca()->set_filename(
      json_tls_context.getString("ca_cert_file", ""));
  if (json_tls_context.hasObject("verify_certificate_hash")) {
    validation_context->add_verify_certificate_hash(
        json_tls_context.getString("verify_certificate_hash"));
  }
  for (const auto& san : json_tls_context.getStringArray("verify_subject_alt_name", true)) {
    validation_context->add_verify_subject_alt_name(san);
  }

  const std::vector<std::string> cipher_suites =
      StringUtil::split(json_tls_context.getString("cipher_suites", ""), ":");
  for (const auto& cipher_suite : cipher_suites) {
    common_tls_context.mutable_tls_params()->add_cipher_suites(cipher_suite);
  }
  const std::vector<std::string> ecdh_curves =
      StringUtil::split(json_tls_context.getString("ecdh_curves", ""), ":");
  for (const auto& ecdh_curve : ecdh_curves) {
    common_tls_context.mutable_tls_params()->add_ecdh_curves(ecdh_curve);
  }
}

void TlsContextJson::translateTlsCertificate(const Json::Object& json_tls_context,
                                             envoy::api::v2::TlsCertificate& tls_certificate) {
  tls_certificate.mutable_certificate_chain()->set_filename(
      json_tls_context.getString("cert_chain_file", ""));
  tls_certificate.mutable_private_key()->set_filename(
      json_tls_context.getString("private_key_file", ""));
}

} // namespace Config
} // namespace Envoy
