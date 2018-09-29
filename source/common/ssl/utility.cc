#include "common/ssl/utility.h"

#include "absl/strings/str_join.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Ssl {

const std::string Utility::getSerialNumberFromCertificate(X509& cert) {
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

const std::vector<std::string> Utility::getSubjectAltNames(X509& cert) {
  std::vector<std::string> subject_alt_names;
  bssl::UniquePtr<GENERAL_NAMES> san_names(
      static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(&cert, NID_subject_alt_name, nullptr, nullptr)));
  if (san_names == nullptr) {
    return subject_alt_names;
  }
  for (const GENERAL_NAME* san : san_names.get()) {
    if (san->type == GEN_DNS) {
      ASN1_STRING* str = san->d.dNSName;
      const char* dns_name = reinterpret_cast<const char*>(ASN1_STRING_data(str));
      subject_alt_names.push_back(std::string(dns_name));
    } else if (san->type == GEN_URI) {
      ASN1_STRING* str = san->d.uniformResourceIdentifier;
      const char* uri = reinterpret_cast<const char*>(ASN1_STRING_data(str));
      subject_alt_names.push_back(std::string(uri));
    }
  }
  return subject_alt_names;
}

const std::string
Utility::formattedSubjectAltNames(const std::vector<std::string>& subject_alt_names) {
  return absl::StrJoin(subject_alt_names, ", ");
}

} // namespace Ssl
} // namespace Envoy
