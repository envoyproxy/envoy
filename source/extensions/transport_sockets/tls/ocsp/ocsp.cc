#include "extensions/transport_sockets/tls/ocsp/ocsp.h"

#include "common/common/utility.h"

#include "extensions/transport_sockets/tls/ocsp/asn1_utility.h"
#include "extensions/transport_sockets/tls/utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace Ocsp {

namespace CertUtility = Envoy::Extensions::TransportSockets::Tls::Utility;

static const unsigned CONS = CBS_ASN1_CONSTRUCTED;
static const unsigned CONT = CBS_ASN1_CONTEXT_SPECIFIC;

namespace {

unsigned parseTag(CBS& cbs) {
  unsigned tag;
  if (!CBS_get_any_asn1_element(&cbs, nullptr, &tag, nullptr)) {
    throw EnvoyException("Failed to parse ASN.1 element tag");
  }
  return tag;
}

std::unique_ptr<OcspResponse> readDerEncodedOcspResponse(const std::string& der) {
  CBS cbs;
  CBS_init(&cbs, reinterpret_cast<const uint8_t*>(der.c_str()), der.size());

  auto resp = Asn1OcspUtility::parseOcspResponse(cbs);
  if (CBS_len(&cbs) != 0) {
    throw EnvoyException("Data contained more than a single OCSP response");
  }

  return resp;
}

void skipResponderId(CBS& cbs) {
  // ResponderID ::= CHOICE {
  //    byName               [1] Name,
  //    byKey                [2] KeyHash
  // }
  //
  // KeyHash ::= OCTET STRING -- SHA-1 hash of responder's public key
  //    (excluding the tag and length fields)

  if (Asn1Utility::isOptionalPresent(cbs, nullptr, CONS | CONT | 1) ||
      Asn1Utility::isOptionalPresent(cbs, nullptr, CONS | CONT | 2)) {
    return;
  }

  throw EnvoyException(absl::StrCat("Unknown choice for Responder ID: ", parseTag(cbs)));
}

} // namespace

OcspResponse::OcspResponse(OcspResponseStatus status, ResponsePtr&& response)
    : status_(status), response_(std::move(response)) {}

BasicOcspResponse::BasicOcspResponse(ResponseData data, std::string signature_alg,
                                     std::vector<uint8_t> signature)
    : data_(data), signature_alg_(signature_alg), signature_(std::move(signature)) {}

const std::string BasicOcspResponse::OID = "1.3.6.1.5.5.7.48.1.1";

ResponseData::ResponseData(Envoy::SystemTime produced_at,
                           std::vector<SingleResponse> single_responses)
    : produced_at_(produced_at), single_responses_(std::move(single_responses)) {}

SingleResponse::SingleResponse(CertId cert_id, CertStatus status, Envoy::SystemTime this_update,
                               absl::optional<Envoy::SystemTime> next_update)
    : cert_id_(cert_id), status_(status), this_update_(this_update), next_update_(next_update) {}

CertId::CertId(std::string serial_number, std::string alg_oid, std::string issuer_name_hash,
               std::string issuer_public_key_hash)
    : serial_number_(serial_number), alg_oid_(alg_oid), issuer_name_hash_(issuer_name_hash),
      issuer_public_key_hash_(issuer_public_key_hash) {}

OcspResponseWrapper::OcspResponseWrapper(std::string der_response, TimeSource& time_source)
    : raw_bytes_(std::move(der_response)), response_(readDerEncodedOcspResponse(raw_bytes_)),
      time_source_(time_source) {

  if (response_->response_ == nullptr) {
    throw EnvoyException("OCSP response has no body");
  }

  // We only permit a 1:1 of certificate to response
  if (response_->response_->getNumCerts() != 1) {
    throw EnvoyException("OCSP Response must be for one certificate only");
  }

  auto& this_update = response_->response_->getThisUpdate();
  if (time_source_.systemTime() < this_update) {
    DateFormatter formatter("%E4Y%m%d%H%M%S");
    throw EnvoyException(absl::StrCat("OCSP Response thisUpdate field is set in the future: ",
                                      formatter.fromTime(this_update)));
  }
}

// TODO(daniel-goldstein): This should also check the issuer
bool OcspResponseWrapper::matchesCertificate(X509& cert) {
  std::string cert_serial_number = CertUtility::getSerialNumberFromCertificate(cert);
  std::string resp_cert_serial_number = response_->response_->getCertSerialNumber();
  return resp_cert_serial_number == cert_serial_number;
}

bool OcspResponseWrapper::isExpired() {
  auto& next_update = response_->response_->getNextUpdate();
  return next_update == absl::nullopt || next_update < time_source_.systemTime();
}

std::unique_ptr<OcspResponse> Asn1OcspUtility::parseOcspResponse(CBS& cbs) {
  // OCSPResponse ::= SEQUENCE {
  //    responseStatus         OCSPResponseStatus,
  //    responseBytes          [0] EXPLICIT ResponseBytes OPTIONAL
  // }

  CBS elem;
  if (!CBS_get_asn1(&cbs, &elem, CBS_ASN1_SEQUENCE)) {
    throw EnvoyException("OCSP Response is not a well-formed ASN.1 SEQUENCE");
  }

  OcspResponseStatus status = Asn1OcspUtility::parseResponseStatus(elem);

  CBS bytes;
  ResponsePtr resp = nullptr;
  if (Asn1Utility::isOptionalPresent(elem, &bytes, CONS | CONT | 0)) {
    resp = Asn1OcspUtility::parseResponseBytes(bytes);
  }

  return std::make_unique<OcspResponse>(status, std::move(resp));
}

OcspResponseStatus Asn1OcspUtility::parseResponseStatus(CBS& cbs) {
  // OCSPResponseStatus ::= ENUMERATED {
  //    successful            (0),  -- Response has valid confirmations
  //    malformedRequest      (1),  -- Illegal confirmation request
  //    internalError         (2),  -- Internal error in issuer
  //    tryLater              (3),  -- Try again later
  //                                -- (4) is not used
  //    sigRequired           (5),  -- Must sign the request
  //    unauthorized          (6)   -- Request unauthorized
  // }
  CBS status;
  if (!CBS_get_asn1(&cbs, &status, CBS_ASN1_ENUMERATED)) {
    throw EnvoyException("OCSP ResponseStatus is not a well-formed ASN.1 ENUMERATED");
  }

  auto status_ordinal = *CBS_data(&status);
  switch (status_ordinal) {
  case 0:
    return OcspResponseStatus::SUCCESSFUL;
  case 1:
    return OcspResponseStatus::MALFORMED_REQUEST;
  case 2:
    return OcspResponseStatus::INTERNAL_ERROR;
  case 3:
    return OcspResponseStatus::TRY_LATER;
  case 5:
    return OcspResponseStatus::SIG_REQUIRED;
  case 6:
    return OcspResponseStatus::UNAUTHORIZED;
  default:
    throw EnvoyException(absl::StrCat("Unknown OCSP Response Status variant: ", status_ordinal));
  }
}

ResponsePtr Asn1OcspUtility::parseResponseBytes(CBS& cbs) {
  // ResponseBytes ::=  SEQUENCE {
  //     responseType        RESPONSE.
  //                             &id ({ResponseSet}),
  //     response            OCTET STRING (CONTAINING RESPONSE.
  //                             &Type({ResponseSet}{@responseType}))
  // }
  CBS elem, response;
  if (!CBS_get_asn1(&cbs, &elem, CBS_ASN1_SEQUENCE)) {
    throw EnvoyException("OCSP ResponseBytes is not a well-formed SEQUENCE");
  }

  auto oid_str = Asn1Utility::parseOid(elem);
  if (!CBS_get_asn1(&elem, &response, CBS_ASN1_OCTETSTRING)) {
    throw EnvoyException("Expected ASN.1 OCTETSTRING for response");
  }

  if (oid_str == BasicOcspResponse::OID) {
    return Asn1OcspUtility::parseBasicOcspResponse(response);
  }
  throw EnvoyException(absl::StrCat("Unknown OCSP Response type with OID: ", oid_str));
}

std::unique_ptr<BasicOcspResponse> Asn1OcspUtility::parseBasicOcspResponse(CBS& cbs) {
  // BasicOCSPResponse       ::= SEQUENCE {
  //    tbsResponseData      ResponseData,
  //    signatureAlgorithm   AlgorithmIdentifier{SIGNATURE-ALGORITHM,
  //                             {sa-dsaWithSHA1 | sa-rsaWithSHA1 |
  //                                  sa-rsaWithMD5 | sa-rsaWithMD2, ...}},
  //    signature            BIT STRING,
  //    certs            [0] EXPLICIT SEQUENCE OF Certificate OPTIONAL
  // }
  CBS elem;
  if (!CBS_get_asn1(&cbs, &elem, CBS_ASN1_SEQUENCE)) {
    throw EnvoyException("OCSP BasicOCSPResponse is not a wellf-formed ASN.1 SEQUENCE");
  }
  auto response_data = Asn1OcspUtility::parseResponseData(elem);
  auto signature_alg = Asn1Utility::parseAlgorithmIdentifier(elem);
  auto signature = Asn1Utility::parseBitString(elem);
  // TODO(daniel-goldstein): Verify this signature
  // "The value for signature SHALL be computed on the hash of the DER
  //    encoding of ResponseData."

  // optional additional certs are ignored.

  return std::make_unique<BasicOcspResponse>(response_data, signature_alg, std::move(signature));
}

ResponseData Asn1OcspUtility::parseResponseData(CBS& cbs) {
  // ResponseData ::= SEQUENCE {
  //    version              [0] EXPLICIT Version DEFAULT v1,
  //    responderID              ResponderID,
  //    producedAt               GeneralizedTime,
  //    responses                SEQUENCE OF SingleResponse,
  //    responseExtensions   [1] EXPLICIT Extensions OPTIONAL
  // }
  CBS elem;
  if (!CBS_get_asn1(&cbs, &elem, CBS_ASN1_SEQUENCE)) {
    throw EnvoyException("OCSP ResponseData is not a well-formed ASN.1 SEQUENCE");
  }

  Asn1Utility::skipOptional(elem, 0);
  skipResponderId(elem);
  auto produced_at = Asn1Utility::parseGeneralizedTime(elem);
  auto responses = Asn1Utility::parseSequenceOf<SingleResponse>(elem, parseSingleResponse);
  // Extensions currently ignored

  return {produced_at, std::move(responses)};
}

SingleResponse Asn1OcspUtility::parseSingleResponse(CBS& cbs) {
  // SingleResponse ::= SEQUENCE {
  //    certID                  CertID,
  //    certStatus              CertStatus,
  //    thisUpdate              GeneralizedTime,
  //    nextUpdate          [0] EXPLICIT GeneralizedTime OPTIONAL,
  //    singleExtensions    [1] EXPLICIT Extensions OPTIONAL
  // }
  CBS elem;
  if (!CBS_get_asn1(&cbs, &elem, CBS_ASN1_SEQUENCE)) {
    throw EnvoyException("OCSP SingleResponse is not a well-formed ASN.1 SEQUENCE");
  }

  auto cert_id = Asn1OcspUtility::parseCertId(elem);
  auto status = Asn1OcspUtility::parseCertStatus(elem);
  auto this_update = Asn1Utility::parseGeneralizedTime(elem);
  auto next_update = Asn1Utility::parseOptional<Envoy::SystemTime>(
      elem, Asn1Utility::parseGeneralizedTime, CONS | CONT | 0);
  // Extensions currently ignored

  return {cert_id, status, this_update, next_update};
}

CertId Asn1OcspUtility::parseCertId(CBS& cbs) {
  // CertID ::= SEQUENCE {
  //    hashAlgorithm       AlgorithmIdentifier,
  //    issuerNameHash      OCTET STRING, -- Hash of issuer's DN
  //    issuerKeyHash       OCTET STRING, -- Hash of issuer's public key
  //    serialNumber        CertificateSerialNumber
  // }
  CBS elem;
  if (!CBS_get_asn1(&cbs, &elem, CBS_ASN1_SEQUENCE)) {
    throw EnvoyException("OCSP CertID is not a well-formed ASN.1 SEQUENCE");
  }

  auto alg = Asn1Utility::parseAlgorithmIdentifier(elem);
  auto issuer_name_hash = Asn1Utility::parseOctetString(elem);
  auto issuer_public_key_hash = Asn1Utility::parseOctetString(elem);
  auto serial_number = Asn1Utility::parseInteger(elem);

  return {serial_number, alg, issuer_name_hash, issuer_public_key_hash};
}

CertStatus Asn1OcspUtility::parseCertStatus(CBS& cbs) {
  // CertStatus ::= CHOICE {
  //    good                [0] IMPLICIT NULL,
  //    revoked             [1] IMPLICIT RevokedInfo,
  //    unknown             [2] IMPLICIT UnknownInfo
  // }
  if (Asn1Utility::isOptionalPresent(cbs, nullptr, CONT | 0)) {
    return CertStatus::GOOD;
  }
  if (Asn1Utility::isOptionalPresent(cbs, nullptr, CONS | CONT | 1)) {
    return CertStatus::REVOKED;
  }
  if (Asn1Utility::isOptionalPresent(cbs, nullptr, CONT | 2)) {
    return CertStatus::UNKNOWN;
  }

  throw EnvoyException(absl::StrCat("Unknown OcspCertStatus tag: ", parseTag(cbs)));
}

} // namespace Ocsp
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
