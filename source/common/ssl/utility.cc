#include "common/ssl/utility.h"

#include "common/common/assert.h"

#include "openssl/bytestring.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Ssl {

std::string Utility::getSerialNumberFromCertificate(X509& cert) {
  ASN1_INTEGER* serial_number = X509_get_serialNumber(&cert);
  BIGNUM num_bn;
  BN_init(&num_bn);
  ASN1_INTEGER_to_BN(serial_number, &num_bn);
  char* char_serial_number = BN_bn2hex(&num_bn);
  BN_free(&num_bn);
  if (char_serial_number != nullptr) {
    std::string serial_number(char_serial_number);
    OPENSSL_free(char_serial_number);
    return serial_number;
  }
  return "";
}

void Utility::parseClientHello(const void* data, size_t len, bssl::UniquePtr<SSL>& ssl_,
                               uint64_t read_, uint32_t maxClientHelloSize,
                               const Ssl::Utility::TlsStats& stats, std::function<void(bool)> done,
                               bool& alpn_found_, bool& clienthello_success_,
                               std::function<void()> onSuccess) {
  // Ownership is passed to ssl_ in SSL_set_bio()
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(data, len));

  // Make the mem-BIO return that there is more data
  // available beyond it's end
  BIO_set_mem_eof_return(bio.get(), -1);

  SSL_set_bio(ssl_.get(), bio.get(), bio.get());
  bio.release();

  int ret = SSL_do_handshake(ssl_.get());

  // This should never succeed because an error is always returned from the SNI callback.
  ASSERT(ret <= 0);
  switch (SSL_get_error(ssl_.get(), ret)) {
  case SSL_ERROR_WANT_READ:
    if (read_ == maxClientHelloSize) {
      // We've hit the specified size limit. This is an unreasonably large ClientHello;
      // indicate failure.
      stats.client_hello_too_large_.inc();
      done(false);
    }
    break;
  case SSL_ERROR_SSL:
    if (clienthello_success_) {
      stats.tls_found_.inc();
      if (alpn_found_) {
        stats.alpn_found_.inc();
      } else {
        stats.alpn_not_found_.inc();
      }
      onSuccess();
    } else {
      stats.tls_not_found_.inc();
    }
    done(true);
    break;
  default:
    done(false);
    break;
  }
}

} // namespace Ssl
} // namespace Envoy
