#include "extensions/transport_sockets/tls/utility.h"

#include "common/common/assert.h"
#include "common/network/address_impl.h"

#include "absl/strings/str_join.h"

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
      subject_alt_names.push_back(generalNameAsString(san));
    }
  }
  return subject_alt_names;
}

std::string Utility::generalNameAsString(const GENERAL_NAME* general_name) {
  std::string san;
  switch (general_name->type) {
  case GEN_DNS: {
    ASN1_STRING* str = general_name->d.dNSName;
    san.assign(reinterpret_cast<const char*>(ASN1_STRING_data(str)), ASN1_STRING_length(str));
    break;
  }
  case GEN_URI: {
    ASN1_STRING* str = general_name->d.uniformResourceIdentifier;
    san.assign(reinterpret_cast<const char*>(ASN1_STRING_data(str)), ASN1_STRING_length(str));
    break;
  }
  case GEN_IPADD: {
    if (general_name->d.ip->length == 4) {
      sockaddr_in sin;
      sin.sin_port = 0;
      sin.sin_family = AF_INET;
      memcpy(&sin.sin_addr, general_name->d.ip->data, sizeof(sin.sin_addr));
      Network::Address::Ipv4Instance addr(&sin);
      san = addr.ip()->addressAsString();
    } else if (general_name->d.ip->length == 16) {
      sockaddr_in6 sin6;
      sin6.sin6_port = 0;
      sin6.sin6_family = AF_INET6;
      memcpy(&sin6.sin6_addr, general_name->d.ip->data, sizeof(sin6.sin6_addr));
      Network::Address::Ipv6Instance addr(sin6);
      san = addr.ip()->addressAsString();
    }
    break;
  }
  }
  return san;
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

absl::optional<std::string> Utility::getX509ExtensionValue(const X509& cert,
                                                           absl::string_view extension_name) {
  X509_EXTENSIONS* extensions(X509_get0_extensions(&cert));

  if (extensions == nullptr) {
    return absl::nullopt;
  }

  const size_t extension_count = sk_X509_EXTENSION_num(extensions);

  for (size_t i = 0; i < extension_count; ++i) {
    X509_EXTENSION* extension = sk_X509_EXTENSION_value(extensions, i);

    ASN1_OBJECT* extension_object = X509_EXTENSION_get_object(extension);
    const size_t size = OBJ_obj2txt(nullptr, 0, extension_object, 0);
    std::vector<char> buffer;
    // +1 to allow for NULL byte.
    buffer.resize(size + 1);
    OBJ_obj2txt(buffer.data(), buffer.size(), extension_object, 0);

    if (absl::string_view(buffer.data(), size) == extension_name) {
      ASN1_OCTET_STRING* octet_string = X509_EXTENSION_get_data(extension);
      const unsigned char* octet_string_data = octet_string->data;
      long xlen;
      int tag, xclass;
      ASN1_get_object(&octet_string_data, &xlen, &tag, &xclass, octet_string->length);

      return std::string(reinterpret_cast<const char*>(octet_string_data), xlen);
    }
  }

  return absl::nullopt;
}

SystemTime Utility::getValidFrom(const X509& cert) {
  int days, seconds;
  int rc = ASN1_TIME_diff(&days, &seconds, &epochASN1_Time(), X509_get0_notBefore(&cert));
  ASSERT(rc == 1);
  // Casting to <time_t (64bit)> to prevent multiplication overflow when certificate valid-from date
  // beyond 2038-01-19T03:14:08Z.
  return std::chrono::system_clock::from_time_t(static_cast<time_t>(days) * 24 * 60 * 60 + seconds);
}

SystemTime Utility::getExpirationTime(const X509& cert) {
  int days, seconds;
  int rc = ASN1_TIME_diff(&days, &seconds, &epochASN1_Time(), X509_get0_notAfter(&cert));
  ASSERT(rc == 1);
  // Casting to <time_t (64bit)> to prevent multiplication overflow when certificate not-after date
  // beyond 2038-01-19T03:14:08Z.
  return std::chrono::system_clock::from_time_t(static_cast<time_t>(days) * 24 * 60 * 60 + seconds);
}

absl::optional<std::string> Utility::getLastCryptoError() {
  auto err = ERR_get_error();

  if (err != 0) {
    char errbuf[256];

    ERR_error_string_n(err, errbuf, sizeof(errbuf));
    return std::string(errbuf);
  }

  return absl::nullopt;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
