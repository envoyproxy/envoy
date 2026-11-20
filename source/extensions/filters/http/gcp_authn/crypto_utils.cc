#include "source/extensions/filters/http/gcp_authn/crypto_utils.h"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/mem.h>
#include <openssl/pem.h>
#include <openssl/sha2.h>
#include <openssl/x509.h>

#include <memory>
#include <string>

#include "source/common/common/base64.h"
#include "source/common/common/logger.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {
namespace {

absl::StatusOr<std::string> calculateFingerprint(X509* cert) {
  unsigned char* der = nullptr;
  int len = i2d_X509(cert, &der);
  if (len <= 0) {
    ENVOY_LOG_MISC(error, "Failed to get DER encoding of certificate");
    return absl::InternalError("Failed to get DER encoding of certificate");
  }
  std::unique_ptr<unsigned char, decltype(&::OPENSSL_free)> free_der(der, ::OPENSSL_free);

  std::vector<uint8_t> digest(SHA256_DIGEST_LENGTH);
  SHA256(der, len, digest.data());

  // Base64 encode unpadded
  return Base64::encode(reinterpret_cast<const char*>(digest.data()), digest.size(), false);
}

} // namespace

absl::StatusOr<std::string>
CertFingerprinterImpl::getFingerprintFromPem(const std::string& pem) const {
  if (pem.empty()) {
    return absl::InvalidArgumentError("Certificate PEM content is empty");
  }

  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(pem.data(), pem.size()));
  bssl::UniquePtr<X509> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
  if (!cert) {
    ERR_clear_error();
    ENVOY_LOG_MISC(error, "Failed to parse certificate from PEM");
    return absl::InvalidArgumentError("Failed to parse certificate from PEM");
  }

  return calculateFingerprint(cert.get());
}

absl::StatusOr<std::string>
CertFingerprinterImpl::getCertChainFromPem(const std::string& pem) const {
  if (pem.empty()) {
    return absl::InvalidArgumentError("Certificate PEM content is empty");
  }

  ERR_clear_error();
  bssl::UniquePtr<BIO> in_bio(BIO_new_mem_buf(pem.data(), pem.size()));
  bssl::UniquePtr<BIO> out_bio(BIO_new(BIO_s_mem()));

  bool found_cert = false;
  while (true) {
    bssl::UniquePtr<X509> cert(PEM_read_bio_X509(in_bio.get(), nullptr, nullptr, nullptr));
    if (!cert) {
      break;
    }
    found_cert = true;
    PEM_write_bio_X509(out_bio.get(), cert.get());
  }

  const uint32_t err = ERR_peek_last_error();
  ERR_clear_error();
  if (!found_cert ||
      !(ERR_GET_LIB(err) == ERR_LIB_PEM && ERR_GET_REASON(err) == PEM_R_NO_START_LINE)) {
    ENVOY_LOG_MISC(error, "Failed to parse certificate chain from PEM");
    return absl::InvalidArgumentError("Failed to parse certificate chain from PEM");
  }

  const uint8_t* output = nullptr;
  size_t length = 0;
  BIO_mem_contents(out_bio.get(), &output, &length);
  return std::string(reinterpret_cast<const char*>(output), length);
}

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
