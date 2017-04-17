#include "common/ssl/context_config_impl.h"

#include "openssl/ssl.h"

namespace Ssl {

const std::string ContextConfigImpl::DEFAULT_CIPHER_SUITES =
#ifdef OPENSSL_IS_BORINGSSL
    "[ECDHE-ECDSA-AES128-GCM-SHA256|ECDHE-ECDSA-CHACHA20-POLY1305]:"
    "[ECDHE-RSA-AES128-GCM-SHA256|ECDHE-RSA-CHACHA20-POLY1305]:"
#else
    "ECDHE-ECDSA-AES128-GCM-SHA256:"
    "ECDHE-RSA-AES128-GCM-SHA256:"
#endif
    "ECDHE-ECDSA-AES128-SHA256:"
    "ECDHE-RSA-AES128-SHA256:"
    "AES128-GCM-SHA256:"
    "AES128-SHA256:"
    "AES128-SHA:"
    "ECDHE-ECDSA-AES256-GCM-SHA384:"
    "ECDHE-RSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-AES256-SHA384:"
    "ECDHE-RSA-AES256-SHA384:"
    "AES256-GCM-SHA384:"
    "AES256-SHA256:"
    "AES256-SHA";

const std::string ContextConfigImpl::DEFAULT_ECDH_CURVES = "X25519:P-256";

ContextConfigImpl::ContextConfigImpl(const Json::Object& config) {
  alpn_protocols_ = config.getString("alpn_protocols", "");
  alt_alpn_protocols_ = config.getString("alt_alpn_protocols", "");
  cipher_suites_ = config.getString("cipher_suites", DEFAULT_CIPHER_SUITES);
  ecdh_curves_ = config.getString("ecdh_curves", DEFAULT_ECDH_CURVES);
  ca_cert_file_ = config.getString("ca_cert_file", "");
  if (config.hasObject("cert_chain_file")) {
    cert_chain_file_ = config.getString("cert_chain_file");
    private_key_file_ = config.getString("private_key_file");
  }
  if (config.hasObject("verify_subject_alt_name")) {
    verify_subject_alt_name_list_ = config.getStringArray("verify_subject_alt_name");
  }
  verify_certificate_hash_ = config.getString("verify_certificate_hash", "");
  server_name_indication_ = config.getString("sni", "");
}

} // Ssl
