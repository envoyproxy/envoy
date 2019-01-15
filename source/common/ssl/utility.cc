#include "common/ssl/utility.h"

#include "common/common/assert.h"
#include "common/common/stack_array.h"

#include "absl/strings/str_join.h"
#include "openssl/evp.h"
#include "openssl/hmac.h"
#include "openssl/sha.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Ssl {

const ASN1_TIME& epochASN1_Time() {
  static ASN1_TIME* e = []() -> ASN1_TIME* {
    ASN1_TIME* epoch = ASN1_TIME_new();
    const time_t epoch_time = 0;
    RELEASE_ASSERT(ASN1_TIME_set(epoch, epoch_time) != NULL, "");
    return epoch;
  }();
  return *e;
}

inline bssl::UniquePtr<ASN1_TIME> currentASN1_Time(TimeSource& time_source) {
  bssl::UniquePtr<ASN1_TIME> current_asn_time(ASN1_TIME_new());
  const time_t current_time = std::chrono::system_clock::to_time_t(time_source.systemTime());
  RELEASE_ASSERT(ASN1_TIME_set(current_asn_time.get(), current_time) != NULL, "");
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

std::string Utility::getSubjectFromCertificate(X509& cert) {
  bssl::UniquePtr<BIO> buf(BIO_new(BIO_s_mem()));
  RELEASE_ASSERT(buf != nullptr, "");

  // flags=XN_FLAG_RFC2253 is the documented parameter for single-line output in RFC 2253 format.
  // Example from the RFC:
  //   * Single value per Relative Distinguished Name (RDN): CN=Steve Kille,O=Isode Limited,C=GB
  //   * Multivalue output in first RDN: OU=Sales+CN=J. Smith,O=Widget Inc.,C=US
  //   * Quoted comma in Organization: CN=L. Eagle,O=Sue\, Grabbit and Runn,C=GB
  X509_NAME_print_ex(buf.get(), X509_get_subject_name(&cert), 0 /* indent */, XN_FLAG_RFC2253);

  const uint8_t* data;
  size_t data_len;
  int rc = BIO_mem_contents(buf.get(), &data, &data_len);
  ASSERT(rc == 1);
  return std::string(reinterpret_cast<const char*>(data), data_len);
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
  EVP_MD_CTX ctx;
  auto rc = EVP_DigestInit(&ctx, EVP_sha256());
  RELEASE_ASSERT(rc == 1, "Failed to init digest context");
  const auto num_slices = buffer.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
  buffer.getRawSlices(slices.begin(), num_slices);
  for (const auto& slice : slices) {
    rc = EVP_DigestUpdate(&ctx, slice.mem_, slice.len_);
    RELEASE_ASSERT(rc == 1, "Failed to update digest");
  }
  unsigned int digest_length;
  rc = EVP_DigestFinal(&ctx, digest.data(), &digest_length);
  RELEASE_ASSERT(rc == 1, "Failed to finalize digest");
  RELEASE_ASSERT(digest_length == SHA256_DIGEST_LENGTH, "Digest length mismatch");
  return digest;
}

std::vector<uint8_t> Utility::getSha256Hmac(const std::vector<uint8_t>& key,
                                            absl::string_view string) {
  std::vector<uint8_t> mac(EVP_MAX_MD_SIZE);
  HMAC_CTX ctx;
  RELEASE_ASSERT(key.size() < std::numeric_limits<int>::max(), "HMAC key is too long");
  HMAC_CTX_init(&ctx);
  auto rc = HMAC_Init_ex(&ctx, key.data(), static_cast<int>(key.size()), EVP_sha256(), nullptr);
  RELEASE_ASSERT(rc == 1, "Failed to init HMAC context");
  rc = HMAC_Update(&ctx, reinterpret_cast<const uint8_t*>(string.data()), string.size());
  RELEASE_ASSERT(rc == 1, "Failed to update HMAC");
  unsigned int len;
  rc = HMAC_Final(&ctx, mac.data(), &len);
  RELEASE_ASSERT(rc == 1, "Failed to finalize HMAC");
  RELEASE_ASSERT(len <= EVP_MAX_MD_SIZE, "HMAC length too large");
  HMAC_CTX_cleanup(&ctx);
  mac.resize(len);
  return mac;
}

} // namespace Ssl
} // namespace Envoy
