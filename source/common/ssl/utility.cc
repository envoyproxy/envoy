#include "common/ssl/utility.h"

#include "common/common/assert.h"

#include "absl/strings/str_join.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Ssl {

inline bssl::UniquePtr<ASN1_TIME> currentASN1_Time(TimeSource& time_source) {
  bssl::UniquePtr<ASN1_TIME> current_asn_time(ASN1_TIME_new());
  time_t current_time = std::chrono::system_clock::to_time_t(time_source.systemTime());
  ASN1_TIME_set(current_asn_time.get(), current_time);
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

int32_t Utility::getDaysUntilExpiration(X509* cert, TimeSource& time_source) {
  if (cert == nullptr) {
    return std::numeric_limits<int>::max();
  }
  int days, seconds;
  if (ASN1_TIME_diff(&days, &seconds, currentASN1_Time(time_source).get(),
                     X509_get_notAfter(cert))) {
    return days;
  }
  return 0;
}

} // namespace Ssl
} // namespace Envoy
