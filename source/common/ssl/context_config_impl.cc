#include "context_config_impl.h"

namespace Ssl {

const std::string ContextConfigImpl::DEFAULT_CIPHER_SUITES = "ECDHE-RSA-AES128-GCM-SHA256:"
                                                             "ECDHE-RSA-AES128-SHA256:"
                                                             "ECDHE-RSA-AES128-SHA:"
                                                             "ECDHE-RSA-AES256-GCM-SHA384:"
                                                             "ECDHE-RSA-AES256-SHA384:"
                                                             "ECDHE-RSA-AES256-SHA:"
                                                             "AES128-GCM-SHA256:"
                                                             "AES256-GCM-SHA384:"
                                                             "AES128-SHA256:"
                                                             "AES256-SHA:"
                                                             "AES128-SHA";

ContextConfigImpl::ContextConfigImpl(const Json::Object& config) {
  alpn_protocols_ = config.getString("alpn_protocols", "");
  alt_alpn_protocols_ = config.getString("alt_alpn_protocols", "");
  cipher_suites_ = config.getString("cipher_suites", DEFAULT_CIPHER_SUITES);
  ca_cert_file_ = config.getString("ca_cert_file", "");
  if (config.hasObject("cert_chain_file")) {
    cert_chain_file_ = config.getString("cert_chain_file");
    private_key_file_ = config.getString("private_key_file");
  }
  if (config.hasObject("verify_subject_alt_name")) {
    verify_subject_alt_name_ = config.getStringArray("verify_subject_alt_name");
  }
  verify_certificate_hash_ = config.getString("verify_certificate_hash", "");
  server_name_indication_ = config.getString("sni", "");
}

} // Ssl
