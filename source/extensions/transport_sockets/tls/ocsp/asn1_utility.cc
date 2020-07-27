#include "extensions/transport_sockets/tls/ocsp/asn1_utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace Ocsp {

std::string Asn1Utility::cbsToString(CBS& cbs) {
  auto str_head = reinterpret_cast<const char*>(CBS_data(&cbs));
  return {str_head, CBS_len(&cbs)};
}

bool Asn1Utility::isOptionalPresent(CBS& cbs, CBS* data, unsigned tag) {
  int is_present;
  if (!CBS_get_optional_asn1(&cbs, data, &is_present, tag)) {
    throw Envoy::EnvoyException("Failed to parse ASN.1 element tag");
  }

  return is_present;
}

std::string Asn1Utility::parseOid(CBS& cbs) {
  CBS oid;
  if (!CBS_get_asn1(&cbs, &oid, CBS_ASN1_OBJECT)) {
    throw Envoy::EnvoyException("Input is not a well-formed ASN.1 OBJECT");
  }
  char* oid_text = CBS_asn1_oid_to_text(&oid);
  if (oid_text == nullptr) {
    throw Envoy::EnvoyException("Failed to parse oid");
  }

  std::string oid_text_str(oid_text);
  OPENSSL_free(oid_text);
  return oid_text_str;
}

Envoy::SystemTime Asn1Utility::parseGeneralizedTime(CBS& cbs) {
  CBS elem;
  if (!CBS_get_asn1(&cbs, &elem, CBS_ASN1_GENERALIZEDTIME)) {
    throw Envoy::EnvoyException("Input is not a well-formed ASN.1 GENERALIZEDTIME");
  }

  auto time_str = cbsToString(elem);
  // OCSP follows the RFC 5280 enforcement that GENERALIZEDTIME
  // fields MUST be in UTC, so must be suffixed with a Z character.
  // Local time or time differential, though a part of the ASN.1
  // GENERALIZEDTIME spec, are not supported.
  // Reference: https://tools.ietf.org/html/rfc5280#section-4.1.2.5.2
  if (time_str.at(time_str.length() - 1) != 'Z') {
    throw Envoy::EnvoyException("GENERALIZEDTIME must be in UTC");
  }

  absl::Time time;
  std::string time_format = "%E4Y%m%d%H%M%SZ";
  std::string parse_error;
  if (!absl::ParseTime(time_format, time_str, &time, &parse_error)) {
    throw Envoy::EnvoyException(absl::StrCat("Error parsing timestamp ", time_str, " with format ",
                                             time_format, ". Error: ", parse_error));
  }
  return absl::ToChronoTime(time);
}

// Performs the following conversions to go from bytestring to hex integer
// CBS -> ASN1_INTEGER -> BIGNUM -> String
std::string Asn1Utility::parseInteger(CBS& cbs) {
  CBS num;
  if (!CBS_get_asn1(&cbs, &num, CBS_ASN1_INTEGER)) {
    throw Envoy::EnvoyException("Input is not a well-formed ASN.1 INTEGER");
  }

  auto head = CBS_data(&num);
  ASN1_INTEGER* asn1_serial_number = c2i_ASN1_INTEGER(nullptr, &head, CBS_len(&num));
  if (asn1_serial_number != nullptr) {
    BIGNUM num_bn;
    BN_init(&num_bn);
    ASN1_INTEGER_to_BN(asn1_serial_number, &num_bn);

    char* char_serial_number = BN_bn2hex(&num_bn);
    BN_free(&num_bn);
    // M_ASN1_INTEGER_free performs a c-style cast which the linters don't
    // like, so we're doing the equivalent here with a static_cast
    ASN1_STRING_free(static_cast<ASN1_STRING*>(asn1_serial_number));

    if (char_serial_number != nullptr) {
      std::string serial_number(char_serial_number);
      OPENSSL_free(char_serial_number);
      return serial_number;
    }
  }

  throw Envoy::EnvoyException("Failed to parse ASN.1 INTEGER");
}

std::string Asn1Utility::parseOctetString(CBS& cbs) {
  CBS value;
  if (!CBS_get_asn1(&cbs, &value, CBS_ASN1_OCTETSTRING)) {
    throw Envoy::EnvoyException("Input is not a well-formed ASN.1 OCTETSTRING");
  }

  return cbsToString(value);
}

std::vector<uint8_t> Asn1Utility::parseBitString(CBS& cbs) {
  CBS value;
  if (!CBS_get_asn1(&cbs, &value, CBS_ASN1_BITSTRING)) {
    throw Envoy::EnvoyException("Input is not a well-formed ASN.1 BITSTRING");
  }

  auto data = reinterpret_cast<const uint8_t*>(CBS_data(&value));
  return {data, data + CBS_len(&value)};
}

std::string Asn1Utility::parseAlgorithmIdentifier(CBS& cbs) {
  // AlgorithmIdentifier  ::=  SEQUENCE  {
  //    algorithm               OBJECT IDENTIFIER,
  //    parameters              ANY DEFINED BY algorithm OPTIONAL
  // }
  CBS elem;
  if (!CBS_get_asn1(&cbs, &elem, CBS_ASN1_SEQUENCE)) {
    throw Envoy::EnvoyException("AlgorithmIdentifier is not a well-formed ASN.1 SEQUENCE");
  }

  return parseOid(elem);
  // ignore parameters
}

void Asn1Utility::skipOptional(CBS& cbs, unsigned tag) {
  if (!CBS_get_optional_asn1(&cbs, nullptr, nullptr, tag)) {
    throw Envoy::EnvoyException("Failed to parse ASN.1 element tag");
  }
}

} // namespace Ocsp
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
