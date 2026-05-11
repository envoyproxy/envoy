#include "source/extensions/filters/http/gcp_authn/crypto_utils.h"

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/mem.h>
#include <openssl/nid.h>
#include <openssl/pem.h>
#include <openssl/sha2.h>
#include <openssl/stack.h>
#include <openssl/x509.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/secret/secret_provider.h"

#include "source/common/common/base64.h"
#include "source/common/common/logger.h"
#include "source/common/common/matchers.h"
#include "source/common/common/thread.h"
#include "source/common/config/datasource.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "re2/re2.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {
namespace {

std::vector<std::string> getSubjectAltNames(X509* cert) {
  std::vector<std::string> sans;
  STACK_OF(GENERAL_NAME)* gens = static_cast<STACK_OF(GENERAL_NAME)*>(
      X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));

  if (gens != nullptr) {
    int count = sk_GENERAL_NAME_num(gens);
    for (int i = 0; i < count; ++i) {
       GENERAL_NAME* gen = sk_GENERAL_NAME_value(gens, i);
      if (gen->type == GEN_URI) {
        ASN1_IA5STRING* uri = gen->d.uniformResourceIdentifier;
        if (uri != nullptr) {
          sans.push_back(std::string(reinterpret_cast<const char*>(ASN1_STRING_get0_data(uri)),
                                     ASN1_STRING_length(uri)));
        }
      }
    }
    GENERAL_NAMES_free(gens);
  }
  return sans;
}

bool validateSubjectAltNames(const std::vector<std::string>& sans,
                             const std::vector<Matchers::StringMatcherImpl>& san_matchers) {
  if (san_matchers.empty()) {
    // Regex match for SAN.
    static const LazyRE2 org_regex = {"^agents\\.global\\.org-\\d+\\.system\\.id\\.goog$"};
    static const LazyRE2 proj_regex = {"^agents\\.global\\.proj-\\d+\\.system\\.id\\.goog$"};

    for (const auto& san : sans) {
      if (RE2::FullMatch(san, *org_regex) || RE2::FullMatch(san, *proj_regex)) {
        return true;
      }
    }
    return false;
  }

  for (const auto& san : sans) {
    for (const auto& matcher : san_matchers) {
      if (matcher.match(san)) {
        return true;
      }
    }
  }
  return false;
}

absl::StatusOr<std::string> calculateFingerprint(X509* cert) {
  unsigned char* der = nullptr;
  int len = i2d_X509(cert, &der);
  if (len < 0) {
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
getBase64EncodedCertificateFingerprint(Secret::TlsCertificateConfigProviderSharedPtr tls_cert_provider,
                          const std::vector<Matchers::StringMatcherImpl>& san_matchers,
                          Api::Api& api) {
  // Config::DataSource::read() is blocking and should only be called on the
  // main thread.
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  if (tls_cert_provider == nullptr) {
    return absl::InvalidArgumentError("TLS certificate provider is null");
  }
  const auto* secret = tls_cert_provider->secret();
  if (secret == nullptr) {
    return absl::NotFoundError("Secret is null");
  }

  // Read certificate from secret.
  const auto& cert_chain = secret->certificate_chain();
  auto file_content_or_error = Config::DataSource::read(cert_chain, true, api);
  if (!file_content_or_error.ok()) {
    return absl::InternalError("Failed to read certificate from data source");
  }
  std::string file_content = file_content_or_error.value();

  if (file_content.empty()) {
    return absl::InvalidArgumentError("Certificate file content is empty");
  }

  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(file_content.data(), file_content.size()));
  bssl::UniquePtr<X509> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
  if (!cert) {
    ENVOY_LOG_MISC(error, "Failed to parse certificate");
    return absl::InvalidArgumentError("Failed to parse certificate");
  }

  std::vector<std::string> sans = getSubjectAltNames(cert.get());
  if (!validateSubjectAltNames(sans, san_matchers)) {
    return absl::InvalidArgumentError("Subject Alternative Names do not match");
  }

  return calculateFingerprint(cert.get());
}

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
