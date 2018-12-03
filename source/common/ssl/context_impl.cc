#include "common/ssl/context_impl.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/common/base64.h"
#include "common/common/fmt.h"
#include "common/common/hex.h"
#include "common/common/utility.h"
#include "common/protobuf/utility.h"
#include "common/ssl/utility.h"

#include "openssl/hmac.h"
#include "openssl/rand.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Ssl {

int ContextImpl::sslContextIndex() {
  CONSTRUCT_ON_FIRST_USE(int, []() -> int {
    int ssl_context_index = SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    RELEASE_ASSERT(ssl_context_index >= 0, "");
    return ssl_context_index;
  }());
}

ContextImpl::ContextImpl(Stats::Scope& scope, const ContextConfig& config, TimeSource& time_source)
    : ctx_(SSL_CTX_new(TLS_method())), scope_(scope), stats_(generateStats(scope)),
      time_source_(time_source) {
  RELEASE_ASSERT(ctx_, "");

  int rc = SSL_CTX_set_ex_data(ctx_.get(), sslContextIndex(), this);
  RELEASE_ASSERT(rc == 1, "");

  rc = SSL_CTX_set_min_proto_version(ctx_.get(), config.minProtocolVersion());
  RELEASE_ASSERT(rc == 1, "");

  rc = SSL_CTX_set_max_proto_version(ctx_.get(), config.maxProtocolVersion());
  RELEASE_ASSERT(rc == 1, "");

  if (!SSL_CTX_set_strict_cipher_list(ctx_.get(), config.cipherSuites().c_str())) {
    std::vector<absl::string_view> ciphers =
        StringUtil::splitToken(config.cipherSuites(), ":+-![|]", false);
    std::vector<std::string> bad_ciphers;
    for (const auto& cipher : ciphers) {
      std::string cipher_str(cipher);
      if (!SSL_CTX_set_strict_cipher_list(ctx_.get(), cipher_str.c_str())) {
        bad_ciphers.push_back(cipher_str);
      }
    }
    throw EnvoyException(fmt::format("Failed to initialize cipher suites {}. The following "
                                     "ciphers were rejected when tried individually: {}",
                                     config.cipherSuites(), StringUtil::join(bad_ciphers, ", ")));
  }

  if (!SSL_CTX_set1_curves_list(ctx_.get(), config.ecdhCurves().c_str())) {
    throw EnvoyException(fmt::format("Failed to initialize ECDH curves {}", config.ecdhCurves()));
  }

  int verify_mode = SSL_VERIFY_NONE;
  if (config.certificateValidationContext() != nullptr &&
      !config.certificateValidationContext()->caCert().empty()) {
    ca_file_path_ = config.certificateValidationContext()->caCertPath();
    bssl::UniquePtr<BIO> bio(
        BIO_new_mem_buf(const_cast<char*>(config.certificateValidationContext()->caCert().data()),
                        config.certificateValidationContext()->caCert().size()));
    RELEASE_ASSERT(bio != nullptr, "");
    // Based on BoringSSL's X509_load_cert_crl_file().
    bssl::UniquePtr<STACK_OF(X509_INFO)> list(
        PEM_X509_INFO_read_bio(bio.get(), nullptr, nullptr, nullptr));
    if (list == nullptr) {
      throw EnvoyException(fmt::format("Failed to load trusted CA certificates from {}",
                                       config.certificateValidationContext()->caCertPath()));
    }

    X509_STORE* store = SSL_CTX_get_cert_store(ctx_.get());
    bool has_crl = false;
    for (const X509_INFO* item : list.get()) {
      if (item->x509) {
        X509_STORE_add_cert(store, item->x509);
        if (ca_cert_ == nullptr) {
          X509_up_ref(item->x509);
          ca_cert_.reset(item->x509);
        }
      }
      if (item->crl) {
        X509_STORE_add_crl(store, item->crl);
        has_crl = true;
      }
    }
    if (ca_cert_ == nullptr) {
      throw EnvoyException(fmt::format("Failed to load trusted CA certificates from {}",
                                       config.certificateValidationContext()->caCertPath()));
    }
    if (has_crl) {
      X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
    }
    verify_mode = SSL_VERIFY_PEER;
    verify_trusted_ca_ = true;

    // NOTE: We're using SSL_CTX_set_cert_verify_callback() instead of X509_verify_cert()
    // directly. However, our new callback is still calling X509_verify_cert() under
    // the hood. Therefore, to ignore cert expiration, we need to set the callback
    // for X509_verify_cert to ignore that error.
    if (config.certificateValidationContext()->allowExpiredCertificate()) {
      X509_STORE_set_verify_cb(store, ContextImpl::ignoreCertificateExpirationCallback);
    }
  }

  if (config.certificateValidationContext() != nullptr &&
      !config.certificateValidationContext()->certificateRevocationList().empty()) {
    bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(
        const_cast<char*>(
            config.certificateValidationContext()->certificateRevocationList().data()),
        config.certificateValidationContext()->certificateRevocationList().size()));
    RELEASE_ASSERT(bio != nullptr, "");

    // Based on BoringSSL's X509_load_cert_crl_file().
    bssl::UniquePtr<STACK_OF(X509_INFO)> list(
        PEM_X509_INFO_read_bio(bio.get(), nullptr, nullptr, nullptr));
    if (list == nullptr) {
      throw EnvoyException(
          fmt::format("Failed to load CRL from {}",
                      config.certificateValidationContext()->certificateRevocationListPath()));
    }

    X509_STORE* store = SSL_CTX_get_cert_store(ctx_.get());
    for (const X509_INFO* item : list.get()) {
      if (item->crl) {
        X509_STORE_add_crl(store, item->crl);
      }
    }

    X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
  }

  if (config.certificateValidationContext() != nullptr &&
      !config.certificateValidationContext()->verifySubjectAltNameList().empty()) {
    verify_subject_alt_name_list_ =
        config.certificateValidationContext()->verifySubjectAltNameList();
    verify_mode = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
  }

  if (config.certificateValidationContext() != nullptr &&
      !config.certificateValidationContext()->verifyCertificateHashList().empty()) {
    for (auto hash : config.certificateValidationContext()->verifyCertificateHashList()) {
      // Remove colons from the 95 chars long colon-separated "fingerprint"
      // in order to get the hex-encoded string.
      if (hash.size() == 95) {
        hash.erase(std::remove(hash.begin(), hash.end(), ':'), hash.end());
      }
      const auto& decoded = Hex::decode(hash);
      if (decoded.size() != SHA256_DIGEST_LENGTH) {
        throw EnvoyException(fmt::format("Invalid hex-encoded SHA-256 {}", hash));
      }
      verify_certificate_hash_list_.push_back(decoded);
    }
    verify_mode = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
  }

  if (config.certificateValidationContext() != nullptr &&
      !config.certificateValidationContext()->verifyCertificateSpkiList().empty()) {
    for (auto hash : config.certificateValidationContext()->verifyCertificateSpkiList()) {
      const auto decoded = Base64::decode(hash);
      if (decoded.size() != SHA256_DIGEST_LENGTH) {
        throw EnvoyException(fmt::format("Invalid base64-encoded SHA-256 {}", hash));
      }
      verify_certificate_spki_list_.emplace_back(decoded.begin(), decoded.end());
    }
    verify_mode = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
  }

  if (verify_mode != SSL_VERIFY_NONE) {
    SSL_CTX_set_verify(ctx_.get(), verify_mode, nullptr);
    SSL_CTX_set_cert_verify_callback(ctx_.get(), ContextImpl::verifyCallback, this);
  }

  const auto tls_certificates = config.tlsCertificates();
  if (!tls_certificates.empty()) {
    // Load certificate chain.
    const auto& tls_certificate = tls_certificates[0].get();
    cert_chain_file_path_ = tls_certificate.certificateChainPath();
    bssl::UniquePtr<BIO> bio(
        BIO_new_mem_buf(const_cast<char*>(tls_certificate.certificateChain().data()),
                        tls_certificate.certificateChain().size()));
    RELEASE_ASSERT(bio != nullptr, "");
    cert_chain_.reset(PEM_read_bio_X509_AUX(bio.get(), nullptr, nullptr, nullptr));
    if (cert_chain_ == nullptr || !SSL_CTX_use_certificate(ctx_.get(), cert_chain_.get())) {
      throw EnvoyException(
          fmt::format("Failed to load certificate chain from {}", cert_chain_file_path_));
    }
    // Read rest of the certificate chain.
    while (true) {
      bssl::UniquePtr<X509> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
      if (cert == nullptr) {
        break;
      }
      if (!SSL_CTX_add_extra_chain_cert(ctx_.get(), cert.get())) {
        throw EnvoyException(
            fmt::format("Failed to load certificate chain from {}", cert_chain_file_path_));
      }
      // SSL_CTX_add_extra_chain_cert() takes ownership.
      cert.release();
    }
    // Check for EOF.
    uint32_t err = ERR_peek_last_error();
    if (ERR_GET_LIB(err) == ERR_LIB_PEM && ERR_GET_REASON(err) == PEM_R_NO_START_LINE) {
      ERR_clear_error();
    } else {
      throw EnvoyException(
          fmt::format("Failed to load certificate chain from {}", cert_chain_file_path_));
    }

    // Load private key.
    bio.reset(BIO_new_mem_buf(const_cast<char*>(tls_certificate.privateKey().data()),
                              tls_certificate.privateKey().size()));
    RELEASE_ASSERT(bio != nullptr, "");
    bssl::UniquePtr<EVP_PKEY> pkey(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
    if (pkey == nullptr || !SSL_CTX_use_PrivateKey(ctx_.get(), pkey.get())) {
      throw EnvoyException(
          fmt::format("Failed to load private key from {}", tls_certificate.privateKeyPath()));
    }
  }

  // use the server's cipher list preferences
  SSL_CTX_set_options(ctx_.get(), SSL_OP_CIPHER_SERVER_PREFERENCE);

  parsed_alpn_protocols_ = parseAlpnProtocols(config.alpnProtocols());
}

int ServerContextImpl::alpnSelectCallback(const unsigned char** out, unsigned char* outlen,
                                          const unsigned char* in, unsigned int inlen) {
  // Currently this uses the standard selection algorithm in priority order.
  const uint8_t* alpn_data = &parsed_alpn_protocols_[0];
  size_t alpn_data_size = parsed_alpn_protocols_.size();

  if (SSL_select_next_proto(const_cast<unsigned char**>(out), outlen, alpn_data, alpn_data_size, in,
                            inlen) != OPENSSL_NPN_NEGOTIATED) {
    return SSL_TLSEXT_ERR_NOACK;
  } else {
    return SSL_TLSEXT_ERR_OK;
  }
}

std::vector<uint8_t> ContextImpl::parseAlpnProtocols(const std::string& alpn_protocols) {
  if (alpn_protocols.empty()) {
    return {};
  }

  if (alpn_protocols.size() >= 65535) {
    throw EnvoyException("invalid ALPN protocol string");
  }

  std::vector<uint8_t> out(alpn_protocols.size() + 1);
  size_t start = 0;
  for (size_t i = 0; i <= alpn_protocols.size(); i++) {
    if (i == alpn_protocols.size() || alpn_protocols[i] == ',') {
      if (i - start > 255) {
        throw EnvoyException("invalid ALPN protocol string");
      }

      out[start] = i - start;
      start = i + 1;
    } else {
      out[i + 1] = alpn_protocols[i];
    }
  }

  return out;
}

bssl::UniquePtr<SSL> ContextImpl::newSsl(absl::optional<std::string>) {
  return bssl::UniquePtr<SSL>(SSL_new(ctx_.get()));
}

int ContextImpl::ignoreCertificateExpirationCallback(int ok, X509_STORE_CTX* ctx) {
  if (!ok) {
    int err = X509_STORE_CTX_get_error(ctx);
    if (err == X509_V_ERR_CERT_HAS_EXPIRED || err == X509_V_ERR_CERT_NOT_YET_VALID) {
      return 1;
    }
  }

  return ok;
}

int ContextImpl::verifyCallback(X509_STORE_CTX* store_ctx, void* arg) {
  ContextImpl* impl = reinterpret_cast<ContextImpl*>(arg);

  if (impl->verify_trusted_ca_) {
    int ret = X509_verify_cert(store_ctx);
    if (ret <= 0) {
      impl->stats_.fail_verify_error_.inc();
      return ret;
    }
  }

  SSL* ssl = reinterpret_cast<SSL*>(
      X509_STORE_CTX_get_ex_data(store_ctx, SSL_get_ex_data_X509_STORE_CTX_idx()));
  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl));
  return impl->verifyCertificate(cert.get());
}

int ContextImpl::verifyCertificate(X509* cert) {
  if (!verify_subject_alt_name_list_.empty() &&
      !verifySubjectAltName(cert, verify_subject_alt_name_list_)) {
    stats_.fail_verify_san_.inc();
    return 0;
  }

  if (!verify_certificate_hash_list_.empty() || !verify_certificate_spki_list_.empty()) {
    const bool valid_certificate_hash =
        !verify_certificate_hash_list_.empty() &&
        verifyCertificateHashList(cert, verify_certificate_hash_list_);
    const bool valid_certificate_spki =
        !verify_certificate_spki_list_.empty() &&
        verifyCertificateSpkiList(cert, verify_certificate_spki_list_);

    if (!valid_certificate_hash && !valid_certificate_spki) {
      stats_.fail_verify_cert_hash_.inc();
      return 0;
    }
  }

  return 1;
}

void ContextImpl::logHandshake(SSL* ssl) const {
  stats_.handshake_.inc();

  if (SSL_session_reused(ssl)) {
    stats_.session_reused_.inc();
  }

  const char* cipher = SSL_get_cipher_name(ssl);
  scope_.counter(fmt::format("ssl.ciphers.{}", std::string{cipher})).inc();

  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl));
  if (!cert.get()) {
    stats_.no_certificate_.inc();
  }
}

bool ContextImpl::verifySubjectAltName(X509* cert,
                                       const std::vector<std::string>& subject_alt_names) {
  bssl::UniquePtr<GENERAL_NAMES> san_names(
      static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr)));
  if (san_names == nullptr) {
    return false;
  }
  for (const GENERAL_NAME* san : san_names.get()) {
    if (san->type == GEN_DNS) {
      ASN1_STRING* str = san->d.dNSName;
      const char* dns_name = reinterpret_cast<const char*>(ASN1_STRING_data(str));
      for (auto& config_san : subject_alt_names) {
        if (dNSNameMatch(config_san, dns_name)) {
          return true;
        }
      }
    } else if (san->type == GEN_URI) {
      ASN1_STRING* str = san->d.uniformResourceIdentifier;
      const char* uri = reinterpret_cast<const char*>(ASN1_STRING_data(str));
      for (auto& config_san : subject_alt_names) {
        if (config_san.compare(uri) == 0) {
          return true;
        }
      }
    }
  }
  return false;
}

bool ContextImpl::dNSNameMatch(const std::string& dNSName, const char* pattern) {
  if (dNSName == pattern) {
    return true;
  }

  size_t pattern_len = strlen(pattern);
  if (pattern_len > 1 && pattern[0] == '*' && pattern[1] == '.') {
    if (dNSName.length() > pattern_len - 1) {
      size_t off = dNSName.length() - pattern_len + 1;
      return dNSName.compare(off, pattern_len - 1, pattern + 1) == 0;
    }
  }

  return false;
}

bool ContextImpl::verifyCertificateHashList(
    X509* cert, const std::vector<std::vector<uint8_t>>& expected_hashes) {
  std::vector<uint8_t> computed_hash(SHA256_DIGEST_LENGTH);
  unsigned int n;
  X509_digest(cert, EVP_sha256(), computed_hash.data(), &n);
  RELEASE_ASSERT(n == computed_hash.size(), "");

  for (const auto& expected_hash : expected_hashes) {
    if (computed_hash == expected_hash) {
      return true;
    }
  }
  return false;
}

bool ContextImpl::verifyCertificateSpkiList(
    X509* cert, const std::vector<std::vector<uint8_t>>& expected_hashes) {
  X509_PUBKEY* pubkey = X509_get_X509_PUBKEY(cert);
  if (pubkey == nullptr) {
    return false;
  }
  uint8_t* spki = nullptr;
  const int len = i2d_X509_PUBKEY(pubkey, &spki);
  if (len < 0) {
    return false;
  }
  bssl::UniquePtr<uint8_t> free_spki(spki);

  std::vector<uint8_t> computed_hash(SHA256_DIGEST_LENGTH);
  SHA256(spki, len, computed_hash.data());

  for (const auto& expected_hash : expected_hashes) {
    if (computed_hash == expected_hash) {
      return true;
    }
  }
  return false;
}

SslStats ContextImpl::generateStats(Stats::Scope& store) {
  std::string prefix("ssl.");
  return {ALL_SSL_STATS(POOL_COUNTER_PREFIX(store, prefix), POOL_GAUGE_PREFIX(store, prefix),
                        POOL_HISTOGRAM_PREFIX(store, prefix))};
}

size_t ContextImpl::daysUntilFirstCertExpires() const {
  int daysUntilExpiration = Utility::getDaysUntilExpiration(ca_cert_.get(), time_source_);
  daysUntilExpiration = std::min<int>(
      Utility::getDaysUntilExpiration(cert_chain_.get(), time_source_), daysUntilExpiration);
  if (daysUntilExpiration < 0) { // Ensure that the return value is unsigned
    return 0;
  }
  return daysUntilExpiration;
}

CertificateDetailsPtr ContextImpl::getCaCertInformation() const {
  if (ca_cert_ == nullptr) {
    return nullptr;
  }
  return certificateDetails(ca_cert_.get(), getCaFileName());
}

CertificateDetailsPtr ContextImpl::getCertChainInformation() const {
  if (cert_chain_ == nullptr) {
    return nullptr;
  }
  return certificateDetails(cert_chain_.get(), getCertChainFileName());
}

CertificateDetailsPtr ContextImpl::certificateDetails(X509* cert, const std::string& path) const {
  CertificateDetailsPtr certificate_details =
      std::make_unique<envoy::admin::v2alpha::CertificateDetails>();
  certificate_details->set_path(path);
  certificate_details->set_serial_number(Utility::getSerialNumberFromCertificate(*cert));
  certificate_details->set_days_until_expiration(
      Utility::getDaysUntilExpiration(cert, time_source_));
  ProtobufWkt::Timestamp* valid_from = certificate_details->mutable_valid_from();
  TimestampUtil::systemClockToTimestamp(Utility::getValidFrom(*cert), *valid_from);
  ProtobufWkt::Timestamp* expiration_time = certificate_details->mutable_expiration_time();
  TimestampUtil::systemClockToTimestamp(Utility::getExpirationTime(*cert), *expiration_time);

  for (auto& dns_san : Utility::getSubjectAltNames(*cert, GEN_DNS)) {
    envoy::admin::v2alpha::SubjectAlternateName& subject_alt_name =
        *certificate_details->add_subject_alt_names();
    subject_alt_name.set_dns(dns_san);
  }
  for (auto& uri_san : Utility::getSubjectAltNames(*cert, GEN_URI)) {
    envoy::admin::v2alpha::SubjectAlternateName& subject_alt_name =
        *certificate_details->add_subject_alt_names();
    subject_alt_name.set_uri(uri_san);
  }
  return certificate_details;
}

ClientContextImpl::ClientContextImpl(Stats::Scope& scope, const ClientContextConfig& config,
                                     TimeSource& time_source)
    : ContextImpl(scope, config, time_source),
      server_name_indication_(config.serverNameIndication()),
      allow_renegotiation_(config.allowRenegotiation()),
      max_session_keys_(config.maxSessionKeys()) {
  if (!parsed_alpn_protocols_.empty()) {
    int rc = SSL_CTX_set_alpn_protos(ctx_.get(), &parsed_alpn_protocols_[0],
                                     parsed_alpn_protocols_.size());
    RELEASE_ASSERT(rc == 0, "");
  }

  if (max_session_keys_ > 0) {
    SSL_CTX_set_session_cache_mode(ctx_.get(), SSL_SESS_CACHE_CLIENT);
    SSL_CTX_sess_set_new_cb(ctx_.get(), [](SSL* ssl, SSL_SESSION* session) -> int {
      ContextImpl* context_impl =
          static_cast<ContextImpl*>(SSL_CTX_get_ex_data(SSL_get_SSL_CTX(ssl), sslContextIndex()));
      ClientContextImpl* client_context_impl = dynamic_cast<ClientContextImpl*>(context_impl);
      RELEASE_ASSERT(client_context_impl != nullptr, ""); // for Coverity
      return client_context_impl->newSessionKey(session);
    });
  }
}

bssl::UniquePtr<SSL> ClientContextImpl::newSsl(absl::optional<std::string> override_server_name) {
  bssl::UniquePtr<SSL> ssl_con(ContextImpl::newSsl(absl::nullopt));

  std::string server_name_indication =
      override_server_name.has_value() ? override_server_name.value() : server_name_indication_;

  if (!server_name_indication.empty()) {
    int rc = SSL_set_tlsext_host_name(ssl_con.get(), server_name_indication.c_str());
    RELEASE_ASSERT(rc, "");
  }

  if (allow_renegotiation_) {
    SSL_set_renegotiate_mode(ssl_con.get(), ssl_renegotiate_freely);
  }

  if (max_session_keys_ > 0) {
    if (session_keys_single_use_) {
      // Stored single-use session keys, use write/write locks.
      absl::WriterMutexLock l(&session_keys_mu_);
      if (!session_keys_.empty()) {
        // Use the most recently stored session key, since it has the highest
        // probability of still being recognized/accepted by the server.
        SSL_SESSION* session = session_keys_.front().get();
        SSL_set_session(ssl_con.get(), session);
        // Remove single-use session key (TLS 1.3) after first use.
        if (SSL_SESSION_should_be_single_use(session)) {
          session_keys_.pop_front();
        }
      }
    } else {
      // Never stored single-use session keys, use read/write locks.
      absl::ReaderMutexLock l(&session_keys_mu_);
      if (!session_keys_.empty()) {
        // Use the most recently stored session key, since it has the highest
        // probability of still being recognized/accepted by the server.
        SSL_SESSION* session = session_keys_.front().get();
        SSL_set_session(ssl_con.get(), session);
      }
    }
  }

  return ssl_con;
}

int ClientContextImpl::newSessionKey(SSL_SESSION* session) {
  // In case we ever store single-use session key (TLS 1.3),
  // we need to switch to using write/write locks.
  if (SSL_SESSION_should_be_single_use(session)) {
    session_keys_single_use_ = true;
  }
  absl::WriterMutexLock l(&session_keys_mu_);
  // Evict oldest entries.
  while (session_keys_.size() >= max_session_keys_) {
    session_keys_.pop_back();
  }
  // Add new session key at the front of the queue, so that it's used first.
  session_keys_.push_front(bssl::UniquePtr<SSL_SESSION>(session));
  return 1; // Tell BoringSSL that we took ownership of the session.
}

ServerContextImpl::ServerContextImpl(Stats::Scope& scope, const ServerContextConfig& config,
                                     const std::vector<std::string>& server_names,
                                     TimeSource& time_source)
    : ContextImpl(scope, config, time_source), session_ticket_keys_(config.sessionTicketKeys()) {
  if (config.tlsCertificates().empty()) {
    throw EnvoyException("Server TlsCertificates must have a certificate specified");
  }
  if (config.certificateValidationContext() != nullptr &&
      !config.certificateValidationContext()->caCert().empty()) {
    bssl::UniquePtr<BIO> bio(
        BIO_new_mem_buf(const_cast<char*>(config.certificateValidationContext()->caCert().data()),
                        config.certificateValidationContext()->caCert().size()));
    RELEASE_ASSERT(bio != nullptr, "");
    // Based on BoringSSL's SSL_add_file_cert_subjects_to_stack().
    bssl::UniquePtr<STACK_OF(X509_NAME)> list(sk_X509_NAME_new(
        [](const X509_NAME** a, const X509_NAME** b) -> int { return X509_NAME_cmp(*a, *b); }));
    RELEASE_ASSERT(list != nullptr, "");
    for (;;) {
      bssl::UniquePtr<X509> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
      if (cert == nullptr) {
        break;
      }
      X509_NAME* name = X509_get_subject_name(cert.get());
      if (name == nullptr) {
        throw EnvoyException(fmt::format("Failed to load trusted client CA certificates from {}",
                                         config.certificateValidationContext()->caCertPath()));
      }
      // Check for duplicates.
      if (sk_X509_NAME_find(list.get(), nullptr, name)) {
        continue;
      }
      bssl::UniquePtr<X509_NAME> name_dup(X509_NAME_dup(name));
      if (name_dup == nullptr || !sk_X509_NAME_push(list.get(), name_dup.release())) {
        throw EnvoyException(fmt::format("Failed to load trusted client CA certificates from {}",
                                         config.certificateValidationContext()->caCertPath()));
      }
    }
    // Check for EOF.
    uint32_t err = ERR_peek_last_error();
    if (ERR_GET_LIB(err) == ERR_LIB_PEM && ERR_GET_REASON(err) == PEM_R_NO_START_LINE) {
      ERR_clear_error();
    } else {
      throw EnvoyException(fmt::format("Failed to load trusted client CA certificates from {}",
                                       config.certificateValidationContext()->caCertPath()));
    }
    SSL_CTX_set_client_CA_list(ctx_.get(), list.release());

    // SSL_VERIFY_PEER or stronger mode was already set in ContextImpl::ContextImpl().
    if (config.requireClientCertificate()) {
      SSL_CTX_set_verify(ctx_.get(), SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
    }
  }

  if (!parsed_alpn_protocols_.empty()) {
    SSL_CTX_set_alpn_select_cb(ctx_.get(),
                               [](SSL*, const unsigned char** out, unsigned char* outlen,
                                  const unsigned char* in, unsigned int inlen, void* arg) -> int {
                                 return static_cast<ServerContextImpl*>(arg)->alpnSelectCallback(
                                     out, outlen, in, inlen);
                               },
                               this);
  }

  if (!session_ticket_keys_.empty()) {
    SSL_CTX_set_tlsext_ticket_key_cb(
        ctx_.get(),
        [](SSL* ssl, uint8_t* key_name, uint8_t* iv, EVP_CIPHER_CTX* ctx, HMAC_CTX* hmac_ctx,
           int encrypt) -> int {
          ContextImpl* context_impl = static_cast<ContextImpl*>(
              SSL_CTX_get_ex_data(SSL_get_SSL_CTX(ssl), sslContextIndex()));
          ServerContextImpl* server_context_impl = dynamic_cast<ServerContextImpl*>(context_impl);
          RELEASE_ASSERT(server_context_impl != nullptr, ""); // for Coverity
          return server_context_impl->sessionTicketProcess(ssl, key_name, iv, ctx, hmac_ctx,
                                                           encrypt);
        });
  }

  uint8_t session_context_buf[EVP_MAX_MD_SIZE] = {};
  unsigned session_context_len = 0;
  EVP_MD_CTX md;
  int rc = EVP_DigestInit(&md, EVP_sha256());
  RELEASE_ASSERT(rc == 1, "");

  // Hash the CommonName/SANs of the server certificate. This makes sure that
  // sessions can only be resumed to a certificate for the same name, but allows
  // resuming to unique certs in the case that different Envoy instances each have
  // their own certs.
  X509* cert = SSL_CTX_get0_certificate(ctx_.get());
  RELEASE_ASSERT(cert != nullptr, "");
  X509_NAME* cert_subject = X509_get_subject_name(cert);
  RELEASE_ASSERT(cert_subject != nullptr, "");
  int cn_index = X509_NAME_get_index_by_NID(cert_subject, NID_commonName, -1);
  // It's possible that the certificate doesn't have CommonName, but has SANs.
  if (cn_index >= 0) {
    X509_NAME_ENTRY* cn_entry = X509_NAME_get_entry(cert_subject, cn_index);
    RELEASE_ASSERT(cn_entry != nullptr, "");
    ASN1_STRING* cn_asn1 = X509_NAME_ENTRY_get_data(cn_entry);
    RELEASE_ASSERT(ASN1_STRING_length(cn_asn1) > 0, "");
    rc = EVP_DigestUpdate(&md, ASN1_STRING_data(cn_asn1), ASN1_STRING_length(cn_asn1));
    RELEASE_ASSERT(rc == 1, "");
  }

  bssl::UniquePtr<GENERAL_NAMES> san_names(
      static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr)));
  if (san_names != nullptr) {
    for (const GENERAL_NAME* san : san_names.get()) {
      if (san->type == GEN_DNS || san->type == GEN_URI) {
        rc = EVP_DigestUpdate(&md, ASN1_STRING_data(san->d.ia5), ASN1_STRING_length(san->d.ia5));
        RELEASE_ASSERT(rc == 1, "");
      }
    }
  } else {
    // Make sure that we have either CommonName or SANs.
    RELEASE_ASSERT(cn_index >= 0, "");
  }

  X509_NAME* cert_issuer_name = X509_get_issuer_name(cert);
  rc = X509_NAME_digest(cert_issuer_name, EVP_sha256(), session_context_buf, &session_context_len);
  RELEASE_ASSERT(rc == 1 && session_context_len == SHA256_DIGEST_LENGTH, "");
  rc = EVP_DigestUpdate(&md, session_context_buf, session_context_len);
  RELEASE_ASSERT(rc == 1, "");

  // Hash all the settings that affect whether the server will allow/accept
  // the client connection. This ensures that the client is always validated against
  // the correct settings, even if session resumption across different listeners
  // is enabled.
  if (ca_cert_ != nullptr) {
    rc = X509_digest(ca_cert_.get(), EVP_sha256(), session_context_buf, &session_context_len);
    RELEASE_ASSERT(rc == 1 && session_context_len == SHA256_DIGEST_LENGTH, "");
    rc = EVP_DigestUpdate(&md, session_context_buf, session_context_len);
    RELEASE_ASSERT(rc == 1, "");

    // verify_subject_alt_name_list_ can only be set with a ca_cert
    for (const std::string& name : verify_subject_alt_name_list_) {
      rc = EVP_DigestUpdate(&md, name.data(), name.size());
      RELEASE_ASSERT(rc == 1, "");
    }
  }

  for (const auto& hash : verify_certificate_hash_list_) {
    rc = EVP_DigestUpdate(&md, hash.data(),
                          hash.size() *
                              sizeof(std::remove_reference<decltype(hash)>::type::value_type));
    RELEASE_ASSERT(rc == 1, "");
  }

  for (const auto& hash : verify_certificate_spki_list_) {
    rc = EVP_DigestUpdate(&md, hash.data(),
                          hash.size() *
                              sizeof(std::remove_reference<decltype(hash)>::type::value_type));
    RELEASE_ASSERT(rc == 1, "");
  }

  // Hash configured SNIs for this context, so that sessions cannot be resumed across different
  // filter chains, even when using the same server certificate.
  for (const auto& name : server_names) {
    rc = EVP_DigestUpdate(&md, name.data(), name.size());
    RELEASE_ASSERT(rc == 1, "");
  }

  rc = EVP_DigestFinal(&md, session_context_buf, &session_context_len);
  RELEASE_ASSERT(rc == 1, "");
  rc = SSL_CTX_set_session_id_context(ctx_.get(), session_context_buf, session_context_len);
  RELEASE_ASSERT(rc == 1, "");
}

int ServerContextImpl::sessionTicketProcess(SSL*, uint8_t* key_name, uint8_t* iv,
                                            EVP_CIPHER_CTX* ctx, HMAC_CTX* hmac_ctx, int encrypt) {
  const EVP_MD* hmac = EVP_sha256();
  const EVP_CIPHER* cipher = EVP_aes_256_cbc();

  if (encrypt == 1) {
    // Encrypt
    RELEASE_ASSERT(session_ticket_keys_.size() >= 1, "");
    // TODO(ggreenway): validate in SDS that session_ticket_keys_ cannot be empty,
    // or if we allow it to be emptied, reconfigure the context so this callback
    // isn't set.

    const ServerContextConfig::SessionTicketKey& key = session_ticket_keys_.front();

    static_assert(std::tuple_size<decltype(key.name_)>::value == SSL_TICKET_KEY_NAME_LEN,
                  "Expected key.name length");
    std::copy_n(key.name_.begin(), SSL_TICKET_KEY_NAME_LEN, key_name);

    int rc = RAND_bytes(iv, EVP_CIPHER_iv_length(cipher));
    ASSERT(rc);

    // This RELEASE_ASSERT is logically a static_assert, but we can't actually get
    // EVP_CIPHER_key_length(cipher) at compile-time
    RELEASE_ASSERT(key.aes_key_.size() == EVP_CIPHER_key_length(cipher), "");
    if (!EVP_EncryptInit_ex(ctx, cipher, nullptr, key.aes_key_.data(), iv)) {
      return -1;
    }

    if (!HMAC_Init_ex(hmac_ctx, key.hmac_key_.data(), key.hmac_key_.size(), hmac, nullptr)) {
      return -1;
    }

    return 1; // success
  } else {
    // Decrypt
    bool is_enc_key = true; // first element is the encryption key
    for (const ServerContextConfig::SessionTicketKey& key : session_ticket_keys_) {
      static_assert(std::tuple_size<decltype(key.name_)>::value == SSL_TICKET_KEY_NAME_LEN,
                    "Expected key.name length");
      if (std::equal(key.name_.begin(), key.name_.end(), key_name)) {
        if (!HMAC_Init_ex(hmac_ctx, key.hmac_key_.data(), key.hmac_key_.size(), hmac, nullptr)) {
          return -1;
        }

        RELEASE_ASSERT(key.aes_key_.size() == EVP_CIPHER_key_length(cipher), "");
        if (!EVP_DecryptInit_ex(ctx, cipher, nullptr, key.aes_key_.data(), iv)) {
          return -1;
        }

        // If our current encryption was not the decryption key, renew
        return is_enc_key ? 1  // success; do not renew
                          : 2; // success: renew key
      }
      is_enc_key = false;
    }

    return 0; // decryption failed
  }
}

} // namespace Ssl
} // namespace Envoy
