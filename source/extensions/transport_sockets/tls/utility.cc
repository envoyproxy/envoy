#include "extensions/transport_sockets/tls/utility.h"

#include "common/common/assert.h"
#include "common/common/stack_array.h"

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "openssl/hmac.h"
#include "openssl/sha.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

namespace {

enum class CertName { Issuer, Subject };

/**
 * Retrieves a name from a certificate and formats it as an RFC 2253 name.
 * @param cert the certificate.
 * @param desired_name the desired name (Issuer or Subject) to retrieve from the certificate.
 * @return std::string returns the desired name formatted as an RFC 2253 name.
 */
std::string getRFC2253NameFromCertificate(X509& cert, CertName desired_name) {
  bssl::UniquePtr<BIO> buf(BIO_new(BIO_s_mem()));
  RELEASE_ASSERT(buf != nullptr, "");

  X509_NAME* name = nullptr;
  switch (desired_name) {
  case CertName::Issuer:
    name = X509_get_issuer_name(&cert);
    break;
  case CertName::Subject:
    name = X509_get_subject_name(&cert);
    break;
  }

  // flags=XN_FLAG_RFC2253 is the documented parameter for single-line output in RFC 2253 format.
  // Example from the RFC:
  //   * Single value per Relative Distinguished Name (RDN): CN=Steve Kille,O=Isode Limited,C=GB
  //   * Multivalue output in first RDN: OU=Sales+CN=J. Smith,O=Widget Inc.,C=US
  //   * Quoted comma in Organization: CN=L. Eagle,O=Sue\, Grabbit and Runn,C=GB
  X509_NAME_print_ex(buf.get(), name, 0 /* indent */, XN_FLAG_RFC2253);

  const uint8_t* data;
  size_t data_len;
  int rc = BIO_mem_contents(buf.get(), &data, &data_len);
  ASSERT(rc == 1);
  return std::string(reinterpret_cast<const char*>(data), data_len);
}

} // namespace

const ASN1_TIME& epochASN1_Time() {
  static ASN1_TIME* e = []() -> ASN1_TIME* {
    ASN1_TIME* epoch = ASN1_TIME_new();
    const time_t epoch_time = 0;
    RELEASE_ASSERT(ASN1_TIME_set(epoch, epoch_time) != nullptr, "");
    return epoch;
  }();
  return *e;
}

inline bssl::UniquePtr<ASN1_TIME> currentASN1_Time(TimeSource& time_source) {
  bssl::UniquePtr<ASN1_TIME> current_asn_time(ASN1_TIME_new());
  const time_t current_time = std::chrono::system_clock::to_time_t(time_source.systemTime());
  RELEASE_ASSERT(ASN1_TIME_set(current_asn_time.get(), current_time) != nullptr, "");
  return current_asn_time;
}

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

std::vector<std::string> Utility::getSubjectAltNames(X509& cert, int type) {
  std::vector<std::string> subject_alt_names;
  bssl::UniquePtr<GENERAL_NAMES> san_names(
      static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(&cert, NID_subject_alt_name, nullptr, nullptr)));
  if (san_names == nullptr) {
    return subject_alt_names;
  }
  for (const GENERAL_NAME* san : san_names.get()) {
    if (san->type == type) {
      ASN1_STRING* str = san->d.dNSName;
      const char* dns_name = reinterpret_cast<const char*>(ASN1_STRING_data(str));
      subject_alt_names.push_back(std::string(dns_name));
    }
  }
  return subject_alt_names;
}

std::string Utility::getIssuerFromCertificate(X509& cert) {
  return getRFC2253NameFromCertificate(cert, CertName::Issuer);
}

std::string Utility::getSubjectFromCertificate(X509& cert) {
  return getRFC2253NameFromCertificate(cert, CertName::Subject);
}

int32_t Utility::getDaysUntilExpiration(const X509* cert, TimeSource& time_source) {
  if (cert == nullptr) {
    return std::numeric_limits<int>::max();
  }
  int days, seconds;
  if (ASN1_TIME_diff(&days, &seconds, currentASN1_Time(time_source).get(),
                     X509_get0_notAfter(cert))) {
    return days;
  }
  return 0;
}

SystemTime Utility::getValidFrom(const X509& cert) {
  int days, seconds;
  int rc = ASN1_TIME_diff(&days, &seconds, &epochASN1_Time(), X509_get0_notBefore(&cert));
  ASSERT(rc == 1);
  return std::chrono::system_clock::from_time_t(days * 24 * 60 * 60 + seconds);
}

SystemTime Utility::getExpirationTime(const X509& cert) {
  int days, seconds;
  int rc = ASN1_TIME_diff(&days, &seconds, &epochASN1_Time(), X509_get0_notAfter(&cert));
  ASSERT(rc == 1);
  return std::chrono::system_clock::from_time_t(days * 24 * 60 * 60 + seconds);
}

std::vector<uint8_t> Utility::getSha256Digest(const Buffer::Instance& buffer) {
  std::vector<uint8_t> digest(SHA256_DIGEST_LENGTH);
  EVP_MD_CTX* ctx(EVP_MD_CTX_new());
  auto rc = EVP_DigestInit(ctx, EVP_sha256());
  RELEASE_ASSERT(rc == 1, "Failed to init digest context");
  const auto num_slices = buffer.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
  buffer.getRawSlices(slices.begin(), num_slices);
  for (const auto& slice : slices) {
    rc = EVP_DigestUpdate(ctx, slice.mem_, slice.len_);
    RELEASE_ASSERT(rc == 1, "Failed to update digest");
  }
  unsigned int len;
  rc = EVP_DigestFinal(ctx, digest.data(), &len);
  RELEASE_ASSERT(rc == 1, "Failed to finalize digest");
  EVP_MD_CTX_free(ctx);
  return digest;
}

std::vector<uint8_t> Utility::getSha256Hmac(const std::vector<uint8_t>& key,
                                            absl::string_view message) {
  std::vector<uint8_t> hmac(SHA256_DIGEST_LENGTH);
  unsigned int len;
  const auto ret =
      HMAC(EVP_sha256(), key.data(), key.size(), reinterpret_cast<const uint8_t*>(message.data()),
           message.size(), hmac.data(), &len);
  RELEASE_ASSERT(ret != nullptr, "Failed to create HMAC");
  return hmac;
}

const VerificationOutput Utility::verifySignature(absl::string_view hash, EVP_PKEY* pubKey,
                                                  const std::vector<uint8_t>& signature,
                                                  const std::vector<uint8_t>& text) {
  // Step 1: initialize EVP_MD_CTX
  bssl::ScopedEVP_MD_CTX ctx;

  // Step 2: initialize EVP_MD
  const EVP_MD* md = Utility::getHashFunction(hash);

  if (md == nullptr) {
    return {false, absl::StrCat(hash, " is not supported.")};
  }

  // Step 3: initialize EVP_DigestVerify
  int ok = EVP_DigestVerifyInit(ctx.get(), nullptr, md, nullptr, pubKey);
  if (!ok) {
    return {false, "Failed to initialize digest verify."};
  }

  // Step 4: verify signature
  ok = EVP_DigestVerify(ctx.get(), signature.data(), signature.size(), text.data(), text.size());

  // Step 5: check result
  if (ok == 1) {
    return {true, ""};
  }

  return {false, absl::StrCat("Failed to verify digest. Error code: ", ok)};
}

PublicKeyPtr Utility::importPublicKey(const std::vector<uint8_t>& key) {
  CBS cbs({key.data(), key.size()});
  return PublicKeyPtr(EVP_parse_public_key(&cbs));
}

const EVP_MD* Utility::getHashFunction(absl::string_view name) {
  const std::string hash = absl::AsciiStrToLower(name);

  // Hash algorithms set refers
  // https://github.com/google/boringssl/blob/master/include/openssl/digest.h
  if (hash == "sha1") {
    return EVP_sha1();
  } else if (hash == "sha224") {
    return EVP_sha224();
  } else if (hash == "sha256") {
    return EVP_sha256();
  } else if (hash == "sha384") {
    return EVP_sha384();
  } else if (hash == "sha512") {
    return EVP_sha512();
  } else {
    return nullptr;
  }
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
