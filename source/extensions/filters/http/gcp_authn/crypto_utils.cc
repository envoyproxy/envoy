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
#include "source/common/http/utility.h"

#include "absl/strings/string_view.h"
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

std::optional<std::string> calculateFingerprint(X509* cert) {
  unsigned char* der = nullptr;
  int len = i2d_X509(cert, &der);
  if (len < 0) {
    ENVOY_LOG_MISC(error, "Failed to get DER encoding of certificate");
    return std::nullopt;
  }
  std::unique_ptr<unsigned char, decltype(&::OPENSSL_free)> free_der(der, ::OPENSSL_free);

  std::vector<uint8_t> digest(SHA256_DIGEST_LENGTH);
  SHA256(der, len, digest.data());

  // Base64 encode unpadded
  std::string base64_fingerprint =
      Base64::encode(reinterpret_cast<const char*>(digest.data()), digest.size(), false);

  // Double encode '+' to '%252B'.
  // First, encode '+' to '%2B'. Other base64 chars like '/' are left unencoded
  std::string encoded_fp = Envoy::Http::Utility::PercentEncoding::encode(base64_fingerprint, "+");
  // Second, encode '%' to '%25', which double encodes the result of the first
  // pass.
  return Envoy::Http::Utility::PercentEncoding::encode(encoded_fp);
}

} // namespace

std::optional<std::string>
getCertificateFingerprint(Secret::TlsCertificateConfigProviderSharedPtr tls_cert_provider,
                          const std::vector<Matchers::StringMatcherImpl>& san_matchers,
                          Api::Api& api) {
  // Config::DataSource::read() is blocking and should only be called on the
  // main thread.
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  if (tls_cert_provider == nullptr) {
    return std::nullopt;
  }
  const auto* secret = tls_cert_provider->secret();
  if (secret == nullptr) {
    return std::nullopt;
  }

  // Read certificate from secret.
  const auto& cert_chain = secret->certificate_chain();
  auto file_content_or_error = Config::DataSource::read(cert_chain, true, api);
  if (!file_content_or_error.ok()) {
    return std::nullopt;
  }
  std::string file_content = file_content_or_error.value();

  if (file_content.empty()) {
    return std::nullopt;
  }

  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(file_content.data(), file_content.size()));
  bssl::UniquePtr<X509> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
  if (!cert) {
    ENVOY_LOG_MISC(error, "Failed to parse certificate");
    return std::nullopt;
  }

  std::vector<std::string> sans = getSubjectAltNames(cert.get());
  if (!validateSubjectAltNames(sans, san_matchers)) {
    return std::nullopt;
  }

  return calculateFingerprint(cert.get());
}

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
