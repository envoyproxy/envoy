#include "source/common/tls/utility.h"

#include <cstdint>
#include <vector>

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/safe_memcpy.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/str_join.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

static constexpr int MAX_OID_LENGTH = 256;

static constexpr absl::string_view SSL_ERROR_UNKNOWN_ERROR_MESSAGE = "UNKNOWN_ERROR";

Envoy::Ssl::CertificateDetailsPtr Utility::certificateDetails(X509* cert, const std::string& path,
                                                              TimeSource& time_source) {
  Envoy::Ssl::CertificateDetailsPtr certificate_details =
      std::make_unique<envoy::admin::v3::CertificateDetails>();
  certificate_details->set_path(path);
  certificate_details->set_serial_number(Utility::getSerialNumberFromCertificate(*cert));
  const auto days_until_expiry = Utility::getDaysUntilExpiration(cert, time_source).value_or(0);
  certificate_details->set_days_until_expiration(days_until_expiry);

  ProtobufWkt::Timestamp* valid_from = certificate_details->mutable_valid_from();
  TimestampUtil::systemClockToTimestamp(Utility::getValidFrom(*cert), *valid_from);
  ProtobufWkt::Timestamp* expiration_time = certificate_details->mutable_expiration_time();
  TimestampUtil::systemClockToTimestamp(Utility::getExpirationTime(*cert), *expiration_time);

  for (auto& dns_san : Utility::getSubjectAltNames(*cert, GEN_DNS)) {
    envoy::admin::v3::SubjectAlternateName& subject_alt_name =
        *certificate_details->add_subject_alt_names();
    subject_alt_name.set_dns(dns_san);
  }
  for (auto& uri_san : Utility::getSubjectAltNames(*cert, GEN_URI)) {
    envoy::admin::v3::SubjectAlternateName& subject_alt_name =
        *certificate_details->add_subject_alt_names();
    subject_alt_name.set_uri(uri_san);
  }
  for (auto& ip_san : Utility::getSubjectAltNames(*cert, GEN_IPADD)) {
    envoy::admin::v3::SubjectAlternateName& subject_alt_name =
        *certificate_details->add_subject_alt_names();
    subject_alt_name.set_ip_address(ip_san);
  }
  return certificate_details;
}

bool Utility::labelWildcardMatch(absl::string_view dns_label, absl::string_view pattern) {
  constexpr char glob = '*';
  // Check the special case of a single * pattern, as it's common.
  if (pattern.size() == 1 && pattern[0] == glob) {
    return true;
  }
  // Only valid if wildcard character appear once.
  if (std::count(pattern.begin(), pattern.end(), glob) == 1) {
    std::vector<absl::string_view> split_pattern = absl::StrSplit(pattern, glob);
    return (pattern.size() <= dns_label.size() + 1) &&
           absl::StartsWith(dns_label, split_pattern[0]) &&
           absl::EndsWith(dns_label, split_pattern[1]);
  }
  return false;
}

bool Utility::dnsNameMatch(absl::string_view dns_name, absl::string_view pattern) {
  // A-label ACE prefix https://www.rfc-editor.org/rfc/rfc5890#section-2.3.2.5.
  constexpr absl::string_view ACE_prefix = "xn--";
  const std::string lower_case_dns_name = absl::AsciiStrToLower(dns_name);
  const std::string lower_case_pattern = absl::AsciiStrToLower(pattern);
  if (lower_case_dns_name == lower_case_pattern) {
    return true;
  }

  std::vector<absl::string_view> split_pattern =
      absl::StrSplit(lower_case_pattern, absl::MaxSplits('.', 1));
  std::vector<absl::string_view> split_dns_name =
      absl::StrSplit(lower_case_dns_name, absl::MaxSplits('.', 1));

  // dns name and pattern should contain more than 1 label to match.
  if (split_pattern.size() < 2 || split_dns_name.size() < 2) {
    return false;
  }
  // Only the left-most label in the pattern contains wildcard '*' and is not an A-label.
  if ((split_pattern[0].find('*') != absl::string_view::npos) &&
      (split_pattern[1].find('*') == absl::string_view::npos) &&
      (!absl::StartsWith(split_pattern[0], ACE_prefix))) {
    return (split_dns_name[1] == split_pattern[1]) &&
           labelWildcardMatch(split_dns_name[0], split_pattern[0]);
  }

  return false;
}

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
  return {reinterpret_cast<const char*>(data), data_len};
}

} // namespace

const ASN1_TIME& epochASN1Time() {
  static ASN1_TIME* e = []() -> ASN1_TIME* {
    ASN1_TIME* epoch = ASN1_TIME_new();
    const time_t epoch_time = 0;
    RELEASE_ASSERT(ASN1_TIME_set(epoch, epoch_time) != nullptr, "");
    return epoch;
  }();
  return *e;
}

inline bssl::UniquePtr<ASN1_TIME> currentASN1Time(TimeSource& time_source) {
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

std::vector<std::string> Utility::getSubjectAltNames(X509& cert, int type, bool skip_unsupported) {
  std::vector<std::string> subject_alt_names;
  bssl::UniquePtr<GENERAL_NAMES> san_names(
      static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(&cert, NID_subject_alt_name, nullptr, nullptr)));
  if (san_names == nullptr) {
    return subject_alt_names;
  }
  for (const GENERAL_NAME* san : san_names.get()) {
    if (san->type == type) {
      if (skip_unsupported) {
        // An IP SAN for an unsupported IP version will throw an exception.
        // TODO(ggreenway): remove this when IP address construction no longer throws.
        TRY_NEEDS_AUDIT_ADDRESS { subject_alt_names.push_back(generalNameAsString(san)); }
        END_TRY CATCH(const EnvoyException& e,
                      { ENVOY_LOG_MISC(debug, "Error reading SAN, value skipped: {}", e.what()); });
      } else {
        subject_alt_names.push_back(generalNameAsString(san));
      }
    }
  }
  return subject_alt_names;
}

std::string Utility::generalNameAsString(const GENERAL_NAME* general_name) {
  std::string san;
  ASN1_STRING* str = nullptr;
  switch (general_name->type) {
  case GEN_DNS:
    str = general_name->d.dNSName;
    san.assign(reinterpret_cast<const char*>(ASN1_STRING_data(str)), ASN1_STRING_length(str));
    break;
  case GEN_URI:
    str = general_name->d.uniformResourceIdentifier;
    san.assign(reinterpret_cast<const char*>(ASN1_STRING_data(str)), ASN1_STRING_length(str));
    break;
  case GEN_EMAIL:
    str = general_name->d.rfc822Name;
    san.assign(reinterpret_cast<const char*>(ASN1_STRING_data(str)), ASN1_STRING_length(str));
    break;
  case GEN_IPADD: {
    if (general_name->d.ip->length == 4) {
      sockaddr_in sin;
      memset(&sin, 0, sizeof(sin));
      sin.sin_port = 0;
      sin.sin_family = AF_INET;
      safeMemcpyUnsafeSrc(&sin.sin_addr, general_name->d.ip->data);
      Network::Address::Ipv4Instance addr(&sin);
      san = addr.ip()->addressAsString();
    } else if (general_name->d.ip->length == 16) {
      sockaddr_in6 sin6;
      memset(&sin6, 0, sizeof(sin6));
      sin6.sin6_port = 0;
      sin6.sin6_family = AF_INET6;
      safeMemcpyUnsafeSrc(&sin6.sin6_addr, general_name->d.ip->data);
      Network::Address::Ipv6Instance addr(sin6);
      san = addr.ip()->addressAsString();
    }
    break;
  }
  case GEN_OTHERNAME: {
    ASN1_TYPE* value = general_name->d.otherName->value;
    if (value == nullptr) {
      break;
    }
    switch (value->type) {
    case V_ASN1_NULL:
      break;
    case V_ASN1_BOOLEAN:
      san = value->value.boolean ? "true" : "false";
      break;
    case V_ASN1_ENUMERATED:
    case V_ASN1_INTEGER: {
      BIGNUM san_bn;
      BN_init(&san_bn);
      value->type == V_ASN1_ENUMERATED ? ASN1_ENUMERATED_to_BN(value->value.enumerated, &san_bn)
                                       : ASN1_INTEGER_to_BN(value->value.integer, &san_bn);
      char* san_char = BN_bn2dec(&san_bn);
      BN_free(&san_bn);
      if (san_char != nullptr) {
        san.assign(san_char);
        OPENSSL_free(san_char);
      }
      break;
    }
    case V_ASN1_OBJECT: {
      char tmp_obj[MAX_OID_LENGTH];
      int obj_len = OBJ_obj2txt(tmp_obj, MAX_OID_LENGTH, value->value.object, 1);
      if (obj_len > MAX_OID_LENGTH || obj_len < 0) {
        break;
      }
      san.assign(tmp_obj);
      break;
    }
    case V_ASN1_BIT_STRING: {
      ASN1_BIT_STRING* tmp_str = value->value.bit_string;
      san.assign(reinterpret_cast<const char*>(ASN1_STRING_data(tmp_str)),
                 ASN1_STRING_length(tmp_str));
      break;
    }
    case V_ASN1_OCTET_STRING: {
      ASN1_OCTET_STRING* tmp_str = value->value.octet_string;
      san.assign(reinterpret_cast<const char*>(ASN1_STRING_data(tmp_str)),
                 ASN1_STRING_length(tmp_str));
      break;
    }
    case V_ASN1_PRINTABLESTRING: {
      ASN1_PRINTABLESTRING* tmp_str = value->value.printablestring;
      san.assign(reinterpret_cast<const char*>(ASN1_STRING_data(tmp_str)),
                 ASN1_STRING_length(tmp_str));
      break;
    }
    case V_ASN1_T61STRING: {
      ASN1_T61STRING* tmp_str = value->value.t61string;
      san.assign(reinterpret_cast<const char*>(ASN1_STRING_data(tmp_str)),
                 ASN1_STRING_length(tmp_str));
      break;
    }
    case V_ASN1_IA5STRING: {
      ASN1_IA5STRING* tmp_str = value->value.ia5string;
      san.assign(reinterpret_cast<const char*>(ASN1_STRING_data(tmp_str)),
                 ASN1_STRING_length(tmp_str));
      break;
    }
    case V_ASN1_GENERALSTRING: {
      ASN1_GENERALSTRING* tmp_str = value->value.generalstring;
      san.assign(reinterpret_cast<const char*>(ASN1_STRING_data(tmp_str)),
                 ASN1_STRING_length(tmp_str));
      break;
    }
    case V_ASN1_BMPSTRING: {
      // `ASN1_BMPSTRING` is encoded using `UCS-4`, which needs conversion to UTF-8.
      unsigned char* tmp = nullptr;
      if (ASN1_STRING_to_UTF8(&tmp, value->value.bmpstring) < 0) {
        break;
      }
      san.assign(reinterpret_cast<const char*>(tmp));
      OPENSSL_free(tmp);
      break;
    }
    case V_ASN1_UNIVERSALSTRING: {
      // `ASN1_UNIVERSALSTRING` is encoded using `UCS-4`, which needs conversion to UTF-8.
      unsigned char* tmp = nullptr;
      if (ASN1_STRING_to_UTF8(&tmp, value->value.universalstring) < 0) {
        break;
      }
      san.assign(reinterpret_cast<const char*>(tmp));
      OPENSSL_free(tmp);
      break;
    }
    case V_ASN1_UTCTIME: {
      ASN1_UTCTIME* tmp_str = value->value.utctime;
      san.assign(reinterpret_cast<const char*>(ASN1_STRING_data(tmp_str)),
                 ASN1_STRING_length(tmp_str));
      break;
    }
    case V_ASN1_GENERALIZEDTIME: {
      ASN1_GENERALIZEDTIME* tmp_str = value->value.generalizedtime;
      san.assign(reinterpret_cast<const char*>(ASN1_STRING_data(tmp_str)),
                 ASN1_STRING_length(tmp_str));
      break;
    }
    case V_ASN1_VISIBLESTRING: {
      ASN1_VISIBLESTRING* tmp_str = value->value.visiblestring;
      san.assign(reinterpret_cast<const char*>(ASN1_STRING_data(tmp_str)),
                 ASN1_STRING_length(tmp_str));
      break;
    }
    case V_ASN1_UTF8STRING: {
      ASN1_UTF8STRING* tmp_str = value->value.utf8string;
      san.assign(reinterpret_cast<const char*>(ASN1_STRING_data(tmp_str)),
                 ASN1_STRING_length(tmp_str));
      break;
    }
    case V_ASN1_SET: {
      ASN1_STRING* tmp_str = value->value.set;
      san.assign(reinterpret_cast<const char*>(ASN1_STRING_data(tmp_str)),
                 ASN1_STRING_length(tmp_str));
      break;
    }
    case V_ASN1_SEQUENCE: {
      ASN1_STRING* tmp_str = value->value.sequence;
      san.assign(reinterpret_cast<const char*>(ASN1_STRING_data(tmp_str)),
                 ASN1_STRING_length(tmp_str));
      break;
    }
    default:
      break;
    }
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

absl::optional<uint32_t> Utility::getDaysUntilExpiration(const X509* cert,
                                                         TimeSource& time_source) {
  if (cert == nullptr) {
    return absl::make_optional(std::numeric_limits<uint32_t>::max());
  }
  int days, seconds;
  if (ASN1_TIME_diff(&days, &seconds, currentASN1Time(time_source).get(),
                     X509_get0_notAfter(cert))) {
    if (days >= 0 && seconds >= 0) {
      return absl::make_optional(days);
    }
  }
  return absl::nullopt;
}

std::vector<std::string> Utility::getCertificateExtensionOids(X509& cert) {
  std::vector<std::string> extension_oids;

  int count = X509_get_ext_count(&cert);
  for (int pos = 0; pos < count; pos++) {
    X509_EXTENSION* extension = X509_get_ext(&cert, pos);
    RELEASE_ASSERT(extension != nullptr, "");

    char oid[MAX_OID_LENGTH];
    int obj_len = OBJ_obj2txt(oid, MAX_OID_LENGTH, X509_EXTENSION_get_object(extension),
                              1 /* always_return_oid */);
    if (obj_len > 0 && obj_len < MAX_OID_LENGTH) {
      extension_oids.push_back(oid);
    }
  }
  return extension_oids;
}

absl::string_view Utility::getCertificateExtensionValue(X509& cert,
                                                        absl::string_view extension_name) {
  bssl::UniquePtr<ASN1_OBJECT> oid(
      OBJ_txt2obj(std::string(extension_name).c_str(), 1 /* don't search names */));
  if (oid == nullptr) {
    return {};
  }

  int pos = X509_get_ext_by_OBJ(&cert, oid.get(), -1);
  if (pos < 0) {
    return {};
  }

  X509_EXTENSION* extension = X509_get_ext(&cert, pos);
  if (extension == nullptr) {
    return {};
  }

  const ASN1_OCTET_STRING* octet_string = X509_EXTENSION_get_data(extension);
  RELEASE_ASSERT(octet_string != nullptr, "");

  // Return the entire DER-encoded value for this extension. Correct decoding depends on
  // knowledge of the expected structure of the extension's value.
  const unsigned char* octet_string_data = ASN1_STRING_get0_data(octet_string);
  const int octet_string_length = ASN1_STRING_length(octet_string);

  return {reinterpret_cast<const char*>(octet_string_data),
          static_cast<absl::string_view::size_type>(octet_string_length)};
}

SystemTime Utility::getValidFrom(const X509& cert) {
  int days, seconds;
  int rc = ASN1_TIME_diff(&days, &seconds, &epochASN1Time(), X509_get0_notBefore(&cert));
  ASSERT(rc == 1);
  // Casting to <time_t (64bit)> to prevent multiplication overflow when certificate valid-from date
  // beyond 2038-01-19T03:14:08Z.
  return std::chrono::system_clock::from_time_t(static_cast<time_t>(days) * 24 * 60 * 60 + seconds);
}

SystemTime Utility::getExpirationTime(const X509& cert) {
  int days, seconds;
  int rc = ASN1_TIME_diff(&days, &seconds, &epochASN1Time(), X509_get0_notAfter(&cert));
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

absl::string_view Utility::getErrorDescription(int err) {
  const char* description = SSL_error_description(err);
  if (description) {
    return description;
  }

  IS_ENVOY_BUG("BoringSSL error had occurred: SSL_error_description() returned nullptr");
  return SSL_ERROR_UNKNOWN_ERROR_MESSAGE;
}

std::string Utility::getX509VerificationErrorInfo(X509_STORE_CTX* ctx) {
  const int n = X509_STORE_CTX_get_error(ctx);
  const int depth = X509_STORE_CTX_get_error_depth(ctx);
  std::string error_details =
      absl::StrCat("X509_verify_cert: certificate verification error at depth ", depth, ": ",
                   X509_verify_cert_error_string(n));
  return error_details;
}

std::vector<std::string> Utility::mapX509Stack(stack_st_X509& stack,
                                               std::function<std::string(X509&)> field_extractor) {
  std::vector<std::string> result;
  if (sk_X509_num(&stack) <= 0) {
    IS_ENVOY_BUG("x509 stack is empty or NULL");
    return result;
  }
  if (field_extractor == nullptr) {
    IS_ENVOY_BUG("field_extractor is nullptr");
    return result;
  }

  for (uint64_t i = 0; i < sk_X509_num(&stack); i++) {
    X509* cert = sk_X509_value(&stack, i);
    if (!cert) {
      result.push_back(""); // Add an empty string so it's clear something was omitted.
    } else {
      result.push_back(field_extractor(*cert));
    }
  }

  return result;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
