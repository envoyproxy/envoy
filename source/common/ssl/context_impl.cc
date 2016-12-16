#include "context_impl.h"

#include "envoy/common/exception.h"
#include "envoy/runtime/runtime.h"

#include "common/common/assert.h"
#include "common/common/hex.h"

#include "openssl/x509v3.h"

namespace Ssl {

/**
 * The following function was generated with 'openssl dhparam -C 2048'
 */
DH* get_dh2048() {
  static unsigned char dh2048_p[] = {
      0xBC, 0x7B, 0xC2, 0x9D, 0xE5, 0x34, 0x1E, 0xD3, 0xD9, 0x8E, 0x31, 0x43, 0x99, 0xD9, 0x30,
      0x6F, 0x1B, 0xFD, 0xB1, 0x2A, 0x8C, 0xFE, 0xD1, 0x99, 0xE0, 0x9F, 0x61, 0xEC, 0xAE, 0x87,
      0xF7, 0x87, 0xAF, 0x8C, 0xDE, 0x1F, 0x89, 0x08, 0x78, 0xF8, 0x9C, 0x26, 0xB1, 0x2E, 0xD3,
      0xF3, 0xEC, 0x7C, 0x79, 0xE3, 0x98, 0x72, 0xDD, 0xB8, 0xAD, 0xCD, 0x75, 0xFA, 0x7E, 0xFC,
      0xD5, 0xAF, 0x18, 0xEF, 0x86, 0x44, 0xC7, 0xA6, 0xCD, 0x06, 0x23, 0x21, 0x77, 0x33, 0xE2,
      0xBC, 0x91, 0xC0, 0x63, 0xBF, 0xD9, 0x4B, 0x31, 0x76, 0x01, 0x6E, 0x41, 0xA3, 0x96, 0x2A,
      0xF8, 0x1E, 0xE4, 0x8E, 0xC3, 0x9A, 0x70, 0x69, 0x03, 0xA8, 0xDA, 0x1A, 0xF1, 0x26, 0xCC,
      0x14, 0x63, 0x7D, 0xAE, 0xDF, 0xBE, 0x02, 0x18, 0x32, 0xBB, 0xEA, 0x73, 0xFF, 0x37, 0x36,
      0xCC, 0x63, 0xBC, 0x23, 0xB1, 0x5E, 0x11, 0xE1, 0x10, 0xFB, 0xE4, 0x7E, 0x2C, 0x60, 0x37,
      0xC9, 0xE0, 0x68, 0xC2, 0x7A, 0x7B, 0xAD, 0x0A, 0x17, 0xB4, 0x45, 0xCD, 0x02, 0xF0, 0x88,
      0xB8, 0xD9, 0x0E, 0x89, 0xA0, 0x2E, 0x39, 0xA6, 0xB7, 0xE2, 0x1F, 0xBA, 0xFC, 0xA3, 0x8A,
      0xB4, 0x35, 0x72, 0x39, 0xC6, 0xC2, 0x67, 0xB9, 0xCD, 0xF2, 0xBF, 0x20, 0xEA, 0xA8, 0xF3,
      0xB0, 0x31, 0xA3, 0x54, 0x39, 0x3B, 0x85, 0xFF, 0xC3, 0x2E, 0x6A, 0x0B, 0xAF, 0x7E, 0x3D,
      0x2A, 0xCD, 0x8A, 0xD7, 0x1E, 0xE3, 0x6C, 0xFE, 0x27, 0x91, 0x03, 0xCD, 0xC7, 0x15, 0xB1,
      0x8C, 0x76, 0xB7, 0x13, 0xB8, 0x9A, 0x2A, 0xCA, 0x2D, 0x3E, 0x14, 0xC9, 0xEF, 0xCC, 0x9D,
      0xB2, 0xFA, 0x06, 0xE6, 0x04, 0xF1, 0x2B, 0x68, 0x61, 0x56, 0x84, 0x00, 0xB9, 0x71, 0x25,
      0x1B, 0xD0, 0x6A, 0x58, 0x63, 0xF6, 0x86, 0x05, 0x04, 0x49, 0x1E, 0xCB, 0x3E, 0x46, 0x96,
      0x93,
  };

  static unsigned char dh2048_g[] = {
      0x02,
  };

  DH* dh = DH_new();
  dh->p = BN_bin2bn(dh2048_p, sizeof(dh2048_p), nullptr);
  dh->g = BN_bin2bn(dh2048_g, sizeof(dh2048_g), nullptr);
  RELEASE_ASSERT(dh->p != nullptr && dh->g != nullptr);
  return dh;
}

const unsigned char ContextImpl::SERVER_SESSION_ID_CONTEXT = 1;

ContextImpl::ContextImpl(const std::string& name, Stats::Store& store, ContextConfig& config)
    : ctx_(SSL_CTX_new(SSLv23_method())), store_(store), stats_prefix_(fmt::format("{}ssl.", name)),
      stats_(generateStats(stats_prefix_, store)) {
  RELEASE_ASSERT(ctx_);
  // the list of ciphers that will be supported
  if (!config.cipherSuites().empty()) {
    const std::string& cipher_suites = config.cipherSuites();

    if (!SSL_CTX_set_cipher_list(ctx_.get(), cipher_suites.c_str())) {
      throw EnvoyException(fmt::format("Failed to initialize cipher suites {}", cipher_suites));
    }

    // verify that all of the specified ciphers were understood by openssl
    ssize_t num_configured = std::count(cipher_suites.begin(), cipher_suites.end(), ':') + 1;
#ifdef OPENSSL_IS_BORINGSSL
    if (sk_SSL_CIPHER_num(ctx_->cipher_list->ciphers) != static_cast<size_t>(num_configured)) {
#else
    if (sk_SSL_CIPHER_num(ctx_->cipher_list) != num_configured) {
#endif
      throw EnvoyException(
          fmt::format("Unknown cipher specified in cipher suites {}", config.cipherSuites()));
    }
  }

  if (!config.caCertFile().empty()) {
    ca_cert_ = loadCert(config.caCertFile());
    ca_file_path_ = config.caCertFile();
    // set CA certificate
    int rc = SSL_CTX_load_verify_locations(ctx_.get(), config.caCertFile().c_str(), nullptr);
    if (0 == rc) {
      throw EnvoyException(
          fmt::format("Failed to load verify locations file {}", config.caCertFile()));
    }

    // This will send an acceptable CA list to browsers which will prevent pop ups.
    rc = SSL_CTX_add_client_CA(ctx_.get(), ca_cert_.get());
    RELEASE_ASSERT(1 == rc);

    // enable peer certificate verification
    SSL_CTX_set_verify(ctx_.get(), SSL_VERIFY_PEER, nullptr);
  }

  if (!config.certChainFile().empty()) {
    cert_chain_ = loadCert(config.certChainFile());
    cert_chain_file_path_ = config.certChainFile();
    int rc = SSL_CTX_use_certificate_chain_file(ctx_.get(), config.certChainFile().c_str());
    if (0 == rc) {
      throw EnvoyException(
          fmt::format("Failed to load certificate chain file {}", config.certChainFile()));
    }

    rc = SSL_CTX_use_PrivateKey_file(ctx_.get(), config.privateKeyFile().c_str(), SSL_FILETYPE_PEM);
    if (0 == rc) {
      throw EnvoyException(
          fmt::format("Failed to load private key file {}", config.privateKeyFile()));
    }
  }

  verify_subject_alt_name_ = config.verifySubjectAltName();

  if (!config.verifyCertificateHash().empty()) {
    std::string hash = config.verifyCertificateHash();
    // remove ':' delimiters from hex string
    hash.erase(std::remove(hash.begin(), hash.end(), ':'), hash.end());
    verify_certificate_hash_ = Hex::decode(hash);
  }

  SSL_CTX_set_options(ctx_.get(), SSL_OP_NO_SSLv2);
  SSL_CTX_set_options(ctx_.get(), SSL_OP_NO_SSLv3);

  // releases buffers when they're no longer needed - saves ~34k per idle connection
  SSL_CTX_set_mode(ctx_.get(), SSL_MODE_RELEASE_BUFFERS);

  // disable SSL compression
  SSL_CTX_set_options(ctx_.get(), SSL_OP_NO_COMPRESSION);

  // use the server's cipher list preferences
  SSL_CTX_set_options(ctx_.get(), SSL_OP_CIPHER_SERVER_PREFERENCE);

  SSL_CTX_set_options(ctx_.get(), SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION);

  // Initialize DH params - 2048 bits was chosen based on recommendations from:
  // https://www.openssl.org/blog/blog/2015/05/20/logjam-freak-upcoming-changes/
  DH* dh = get_dh2048();
#ifdef OPENSSL_IS_BORINGSSL
  long rc = SSL_CTX_set_tmp_dh(ctx_.get(), dh);
#else
  long rc = SSL_CTX_ctrl(ctx_.get(), SSL_CTRL_SET_TMP_DH, 0, reinterpret_cast<char*>(dh));
#endif
  DH_free(dh);

  // As of openssl 1.0.2f this is on by default and cannot be disabled. Set it here anyway.
  SSL_CTX_set_options(ctx_.get(), SSL_OP_SINGLE_DH_USE);

  if (1 != rc) {
    throw EnvoyException(fmt::format("Failed to initialize DH params"));
  }

  // Initialize elliptic curve - this curve was chosen to match the one currently supported by ELB
  EC_KEY* ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
  if (!ecdh) {
    throw EnvoyException(fmt::format("Failed to initialize elliptic curve"));
  }

#ifdef OPENSSL_IS_BORINGSSL
  rc = SSL_CTX_set_tmp_ecdh(ctx_.get(), ecdh);
#else
  rc = SSL_CTX_ctrl(ctx_.get(), SSL_CTRL_SET_TMP_ECDH, 0, reinterpret_cast<char*>(ecdh));
#endif
  EC_KEY_free(ecdh);

  if (1 != rc) {
    throw EnvoyException(fmt::format("Failed to initialize elliptic curve"));
  }

  SSL_CTX_set_session_id_context(ctx_.get(), &SERVER_SESSION_ID_CONTEXT,
                                 sizeof SERVER_SESSION_ID_CONTEXT);

  parsed_alpn_protocols_ = parseAlpnProtocols(config.alpnProtocols());
}

int ServerContextImpl::alpnSelectCallback(const unsigned char** out, unsigned char* outlen,
                                          const unsigned char* in, unsigned int inlen) {
  // Currently this uses the standard selection algorithm in priority order.
  const uint8_t* alpn_data = &parsed_alpn_protocols_[0];
  size_t alpn_data_size = parsed_alpn_protocols_.size();
  if (!parsed_alt_alpn_protocols_.empty() &&
      runtime_.snapshot().featureEnabled("ssl.alt_alpn", 0)) {
    alpn_data = &parsed_alt_alpn_protocols_[0];
    alpn_data_size = parsed_alt_alpn_protocols_.size();
  }

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

SslConPtr ContextImpl::newSsl() const { return SSL_new(ctx_.get()); }

bool ContextImpl::verifyPeer(SSL* ssl) const {
  bool verified = true;

  stats_.handshake_.inc();

  const char* cipher = SSL_get_cipher_name(ssl);
  store_.counter(fmt::format("{}ciphers.{}", stats_prefix_, std::string{cipher})).inc();

  X509Ptr cert = X509Ptr(SSL_get_peer_certificate(ssl));

  if (!cert.get()) {
    stats_.no_certificate_.inc();
  }

  if (!verify_subject_alt_name_.empty()) {
    if (cert.get() == nullptr || !verifySubjectAltName(cert.get(), verify_subject_alt_name_)) {
      stats_.fail_verify_san_.inc();
      verified = false;
    }
  }

  if (!verify_certificate_hash_.empty()) {
    if (cert.get() == nullptr || !verifyCertificateHash(cert.get(), verify_certificate_hash_)) {
      stats_.fail_verify_cert_hash_.inc();
      verified = false;
    }
  }

  return verified;
}

bool ContextImpl::verifySubjectAltName(X509* cert, const std::string& subject_alt_name) {
  bool verified = false;

  STACK_OF(GENERAL_NAME)* altnames = static_cast<STACK_OF(GENERAL_NAME)*>(
      X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));

  if (altnames) {
    int n = sk_GENERAL_NAME_num(altnames);
    for (int i = 0; i < n; i++) {
      GENERAL_NAME* altname = sk_GENERAL_NAME_value(altnames, i);

      if (altname->type == GEN_DNS) {
        ASN1_STRING* str = altname->d.dNSName;
        char* dns_name = reinterpret_cast<char*>(ASN1_STRING_data(str));
        if (sanMatch(subject_alt_name, dns_name)) {
          verified = true;
          break;
        }
      }
    }

    sk_GENERAL_NAME_pop_free(altnames, GENERAL_NAME_free);
  }

  return verified;
}

bool ContextImpl::sanMatch(const std::string& san, const char* pattern) {
  if (san == pattern) {
    return true;
  }

  size_t pattern_len = strlen(pattern);
  if (pattern_len > 1 && pattern[0] == '*' && pattern[1] == '.') {
    if (san.length() > pattern_len - 1) {
      size_t off = san.length() - pattern_len + 1;
      return san.compare(off, pattern_len - 1, pattern + 1) == 0;
    }
  }

  return false;
}

bool ContextImpl::verifyCertificateHash(X509* cert, const std::vector<uint8_t>& expected_hash) {
  std::vector<uint8_t> computed_hash(SHA256_DIGEST_LENGTH);
  unsigned int n;
  X509_digest(cert, EVP_sha256(), computed_hash.data(), &n);
  RELEASE_ASSERT(n == computed_hash.size());

  return computed_hash == expected_hash;
}

SslStats ContextImpl::generateStats(const std::string& prefix, Stats::Store& store) {
  return {ALL_SSL_STATS(POOL_COUNTER_PREFIX(store, prefix), POOL_GAUGE_PREFIX(store, prefix),
                        POOL_TIMER_PREFIX(store, prefix))};
}

size_t ContextImpl::daysUntilFirstCertExpires() {
  int daysUntilExpiration = getDaysUntilExpiration(ca_cert_.get());
  daysUntilExpiration =
      std::min<int>(getDaysUntilExpiration(cert_chain_.get()), daysUntilExpiration);
  if (daysUntilExpiration < 0) { // Ensure that the return value is unsigned
    return 0;
  }
  return daysUntilExpiration;
}

int32_t ContextImpl::getDaysUntilExpiration(const X509* cert) {
  if (cert == nullptr) {
    return std::numeric_limits<int>::max();
  }
  int days, seconds;
  if (ASN1_TIME_diff(&days, &seconds, nullptr, X509_get_notAfter(cert))) {
    return days;
  }
  return 0;
}

std::string ContextImpl::getCaCertInformation() {
  if (ca_cert_ == nullptr) {
    return "";
  }
  return fmt::format("Certificate Path: {}, Serial Number: {}, Days until Expiration: {}",
                     getCaFileName(), getSerialNumber(ca_cert_.get()),
                     getDaysUntilExpiration(ca_cert_.get()));
}

std::string ContextImpl::getCertChainInformation() {
  if (cert_chain_ == nullptr) {
    return "";
  }
  return fmt::format("Certificate Path: {}, Serial Number: {}, Days until Expiration: {}",
                     getCertChainFileName(), getSerialNumber(cert_chain_.get()),
                     getDaysUntilExpiration(cert_chain_.get()));
}

std::string ContextImpl::getSerialNumber(X509* cert) {
  ASSERT(cert);
  ASN1_INTEGER* serial_number = X509_get_serialNumber(cert);
  BIGNUM num_bn;
  BN_init(&num_bn);
  ASN1_INTEGER_to_BN(serial_number, &num_bn);
  char* char_serial_number = BN_bn2hex(&num_bn);
  BN_free(&num_bn);
  if (char_serial_number != nullptr) {
    std::string serial_number(char_serial_number);
    OPENSSL_free(char_serial_number);
#ifdef OPENSSL_IS_BORINGSSL
    std::transform(serial_number.begin(), serial_number.end(), serial_number.begin(), ::toupper);
#endif
    return serial_number;
  }
  return "";
}

X509Ptr ContextImpl::loadCert(const std::string& cert_file) {
  X509* cert = nullptr;
  std::unique_ptr<FILE, decltype(&fclose)> fp(fopen(cert_file.c_str(), "r"), &fclose);
  if (!fp.get() || !PEM_read_X509(fp.get(), &cert, nullptr, nullptr)) {
    throw EnvoyException(fmt::format("Failed to load certificate '{}'", cert_file.c_str()));
  }
  return X509Ptr{cert};
};

ClientContextImpl::ClientContextImpl(const std::string& name, Stats::Store& stats,
                                     ContextConfig& config)
    : ContextImpl(name, stats, config) {
  if (!parsed_alpn_protocols_.empty()) {
    int rc = SSL_CTX_set_alpn_protos(ctx_.get(), &parsed_alpn_protocols_[0],
                                     parsed_alpn_protocols_.size());
    RELEASE_ASSERT(rc == 0);
    UNREFERENCED_PARAMETER(rc);
  }

  server_name_indication_ = config.serverNameIndication();
}

SslConPtr ClientContextImpl::newSsl() const {
  SslConPtr ssl_con = SslConPtr(ContextImpl::newSsl());

  if (!server_name_indication_.empty()) {
#ifdef OPENSSL_IS_BORINGSSL
    int rc = SSL_set_tlsext_host_name(ssl_con.get(), server_name_indication_.c_str());
#else
    int rc = SSL_ctrl(ssl_con.get(), SSL_CTRL_SET_TLSEXT_HOSTNAME, TLSEXT_NAMETYPE_host_name,
                      const_cast<char*>(server_name_indication_.c_str()));
#endif
    RELEASE_ASSERT(rc);
    UNREFERENCED_PARAMETER(rc);
  }

  return ssl_con;
}

ServerContextImpl::ServerContextImpl(const std::string& name, Stats::Store& stats,
                                     ContextConfig& config, Runtime::Loader& runtime)
    : ContextImpl(name, stats, config), runtime_(runtime) {
  parsed_alt_alpn_protocols_ = parseAlpnProtocols(config.altAlpnProtocols());

  if (!parsed_alpn_protocols_.empty()) {
    SSL_CTX_set_alpn_select_cb(ctx_.get(),
                               [](SSL*, const unsigned char** out, unsigned char* outlen,
                                  const unsigned char* in, unsigned int inlen, void* arg) -> int {
                                 return static_cast<ServerContextImpl*>(arg)
                                     ->alpnSelectCallback(out, outlen, in, inlen);
                               },
                               this);
  }
}

} // Ssl
