#include "common/config/tls_context_json.h"

#include "envoy/api/v2/auth/cert.pb.validate.h"

#include "common/common/utility.h"
#include "common/config/json_utility.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

void TlsContextJson::translateDownstreamTlsContext(
    const Json::Object& json_tls_context,
    envoy::api::v2::auth::DownstreamTlsContext& downstream_tls_context) {
  translateCommonTlsContext(json_tls_context, *downstream_tls_context.mutable_common_tls_context());
  JSON_UTIL_SET_BOOL(json_tls_context, downstream_tls_context, require_client_certificate);

  const std::vector<std::string> paths =
      json_tls_context.getStringArray("session_ticket_key_paths", true);
  for (const std::string& path : paths) {
    downstream_tls_context.mutable_session_ticket_keys()->mutable_keys()->Add()->set_filename(path);
  }
  MessageUtil::validate(downstream_tls_context);
}

void TlsContextJson::translateUpstreamTlsContext(
    const Json::Object& json_tls_context,
    envoy::api::v2::auth::UpstreamTlsContext& upstream_tls_context) {
  translateCommonTlsContext(json_tls_context, *upstream_tls_context.mutable_common_tls_context());
  upstream_tls_context.set_sni(json_tls_context.getString("sni", ""));
  MessageUtil::validate(upstream_tls_context);
}

void TlsContextJson::translateCommonTlsContext(
    const Json::Object& json_tls_context,
    envoy::api::v2::auth::CommonTlsContext& common_tls_context) {
  const std::string alpn_protocols_str{json_tls_context.getString("alpn_protocols", "")};
  for (auto alpn_protocol : StringUtil::splitToken(alpn_protocols_str, ",")) {
    common_tls_context.add_alpn_protocols(std::string{alpn_protocol});
  }

  translateTlsCertificate(json_tls_context, *common_tls_context.mutable_tls_certificates()->Add());

  auto* validation_context = common_tls_context.mutable_validation_context();
  if (json_tls_context.hasObject("ca_cert_file")) {
    validation_context->mutable_trusted_ca()->set_filename(
        json_tls_context.getString("ca_cert_file", ""));
  }
  if (json_tls_context.hasObject("crl_file")) {
    validation_context->mutable_crl()->set_filename(json_tls_context.getString("crl_file", ""));
  }
  if (json_tls_context.hasObject("verify_certificate_hash")) {
    validation_context->add_verify_certificate_hash(
        json_tls_context.getString("verify_certificate_hash"));
  }
  for (const auto& san : json_tls_context.getStringArray("verify_subject_alt_name", true)) {
    validation_context->add_verify_subject_alt_name(san);
  }

  const std::string cipher_suites_str{json_tls_context.getString("cipher_suites", "")};
  for (auto cipher_suite : StringUtil::splitToken(cipher_suites_str, ":")) {
    common_tls_context.mutable_tls_params()->add_cipher_suites(std::string{cipher_suite});
  }

  const std::string ecdh_curves_str{json_tls_context.getString("ecdh_curves", "")};
  for (auto ecdh_curve : StringUtil::splitToken(ecdh_curves_str, ":")) {
    common_tls_context.mutable_tls_params()->add_ecdh_curves(std::string{ecdh_curve});
  }
}

void TlsContextJson::translateTlsCertificate(
    const Json::Object& json_tls_context, envoy::api::v2::auth::TlsCertificate& tls_certificate) {
  if (json_tls_context.hasObject("cert_chain_file")) {
    tls_certificate.mutable_certificate_chain()->set_filename(
        json_tls_context.getString("cert_chain_file", ""));
  }
  if (json_tls_context.hasObject("private_key_file")) {
    tls_certificate.mutable_private_key()->set_filename(
        json_tls_context.getString("private_key_file", ""));
  }
}

} // namespace Config
} // namespace Envoy
