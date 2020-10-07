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

namespace {

template <typename T> T unwrap(ParsingResult<T> res) {
  if (absl::holds_alternative<T>(res)) {
    return absl::get<0>(res);
  }

  throw EnvoyException(std::string(absl::get<1>(res)));
}

unsigned parseTag(CBS& cbs) {
  unsigned tag;
  if (!CBS_get_any_asn1_element(&cbs, nullptr, &tag, nullptr)) {
    throw EnvoyException("Failed to parse ASN.1 element tag");
  }
  return tag;
}

std::unique_ptr<OcspResponse> readDerEncodedOcspResponse(const std::vector<uint8_t>& der) {
  CBS cbs;
  CBS_init(&cbs, der.data(), der.size());

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

  if (unwrap(Asn1Utility::getOptional(cbs, CBS_ASN1_CONSTRUCTED | CBS_ASN1_CONTEXT_SPECIFIC | 1)) ||
      unwrap(Asn1Utility::getOptional(cbs, CBS_ASN1_CONSTRUCTED | CBS_ASN1_CONTEXT_SPECIFIC | 2))) {
    return;
  }

  throw EnvoyException(absl::StrCat("Unknown choice for Responder ID: ", parseTag(cbs)));
}

void skipCertStatus(CBS& cbs) {
  // CertStatus ::= CHOICE {
  //  good                [0] IMPLICIT NULL,
  //  revoked             [1] IMPLICIT RevokedInfo,
  //  unknown             [2] IMPLICIT UnknownInfo
  // }
  if (!(unwrap(Asn1Utility::getOptional(cbs, CBS_ASN1_CONTEXT_SPECIFIC | 0)) ||
        unwrap(
            Asn1Utility::getOptional(cbs, CBS_ASN1_CONSTRUCTED | CBS_ASN1_CONTEXT_SPECIFIC | 1)) ||
        unwrap(Asn1Utility::getOptional(cbs, CBS_ASN1_CONTEXT_SPECIFIC | 2)))) {
    throw EnvoyException(absl::StrCat("Unknown OcspCertStatus tag: ", parseTag(cbs)));
  }
}

} // namespace

OcspResponse::OcspResponse(OcspResponseStatus status, ResponsePtr response)
    : status_(status), response_(std::move(response)) {}

BasicOcspResponse::BasicOcspResponse(ResponseData data) : data_(data) {}

ResponseData::ResponseData(std::vector<SingleResponse> single_responses)
    : single_responses_(std::move(single_responses)) {}

SingleResponse::SingleResponse(CertId cert_id, Envoy::SystemTime this_update,
                               absl::optional<Envoy::SystemTime> next_update)
    : cert_id_(cert_id), this_update_(this_update), next_update_(next_update) {}

CertId::CertId(std::string serial_number) : serial_number_(serial_number) {}

OcspResponseWrapper::OcspResponseWrapper(std::vector<uint8_t> der_response, TimeSource& time_source)
    : raw_bytes_(std::move(der_response)), response_(readDerEncodedOcspResponse(raw_bytes_)),
      time_source_(time_source) {

  if (response_->status_ != OcspResponseStatus::Successful) {
    throw EnvoyException("OCSP response was unsuccessful");
  }

  if (response_->response_ == nullptr) {
    throw EnvoyException("OCSP response has no body");
  }

  // We only permit a 1:1 of certificate to response.
  if (response_->response_->getNumCerts() != 1) {
    throw EnvoyException("OCSP Response must be for one certificate only");
  }

  auto& this_update = response_->response_->getThisUpdate();
  if (time_source_.systemTime() < this_update) {
    std::string time_format(GENERALIZED_TIME_FORMAT);
    DateFormatter formatter(time_format);
    ENVOY_LOG_MISC(warn, "OCSP Response thisUpdate field is set in the future: {}",
                   formatter.fromTime(this_update));
  }
}

// We use just the serial number to uniquely identify a certificate.
// Though different issuers could produce certificates with the same serial
// number, this is check is to prevent operator error and a collision in this
// case is unlikely.
bool OcspResponseWrapper::matchesCertificate(X509& cert) const {
  std::string cert_serial_number = CertUtility::getSerialNumberFromCertificate(cert);
  std::string resp_cert_serial_number = response_->response_->getCertSerialNumber();
  return resp_cert_serial_number == cert_serial_number;
}

bool OcspResponseWrapper::isExpired() {
  auto& next_update = response_->response_->getNextUpdate();
  return next_update == absl::nullopt || next_update < time_source_.systemTime();
}

uint64_t OcspResponseWrapper::secondsUntilExpiration() const {
  auto& next_update = response_->response_->getNextUpdate();
  auto now = time_source_.systemTime();
  if (!next_update || next_update.value() <= now) {
    return 0;
  }
  return std::chrono::duration_cast<std::chrono::seconds>(next_update.value() - now).count();
}

Envoy::SystemTime OcspResponseWrapper::getThisUpdate() const {
  return response_->response_->getThisUpdate();
}

Envoy::SystemTime OcspResponseWrapper::getNextUpdate() const {
  auto& next_update = response_->response_->getNextUpdate();
  if (next_update) {
    return *next_update;
  }

  return time_source_.systemTime();
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
  auto maybe_bytes =
      unwrap(Asn1Utility::getOptional(elem, CBS_ASN1_CONSTRUCTED | CBS_ASN1_CONTEXT_SPECIFIC | 0));
  ResponsePtr resp = nullptr;
  if (maybe_bytes) {
    resp = Asn1OcspUtility::parseResponseBytes(maybe_bytes.value());
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
    return OcspResponseStatus::Successful;
  case 1:
    return OcspResponseStatus::MalformedRequest;
  case 2:
    return OcspResponseStatus::InternalError;
  case 3:
    return OcspResponseStatus::TryLater;
  case 5:
    return OcspResponseStatus::SigRequired;
  case 6:
    return OcspResponseStatus::Unauthorized;
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

  auto oid_str = unwrap(Asn1Utility::parseOid(elem));
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
  //                             {`sa-dsaWithSHA1` | `sa-rsaWithSHA1` |
  //                                  `sa-rsaWithMD5` | `sa-rsaWithMD2`, ...}},
  //    signature            BIT STRING,
  //    certs            [0] EXPLICIT SEQUENCE OF Certificate OPTIONAL
  // }
  CBS elem;
  if (!CBS_get_asn1(&cbs, &elem, CBS_ASN1_SEQUENCE)) {
    throw EnvoyException("OCSP BasicOCSPResponse is not a wellf-formed ASN.1 SEQUENCE");
  }
  auto response_data = Asn1OcspUtility::parseResponseData(elem);
  // The `signatureAlgorithm` and `signature` are ignored because OCSP
  // responses are expected to be delivered from a reliable source.
  // Optional additional certs are ignored.

  return std::make_unique<BasicOcspResponse>(response_data);
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

  unwrap(Asn1Utility::skipOptional(elem, 0));
  skipResponderId(elem);
  unwrap(Asn1Utility::skip(elem, CBS_ASN1_GENERALIZEDTIME));
  auto responses = unwrap(Asn1Utility::parseSequenceOf<SingleResponse>(
      elem, [](CBS& cbs) -> ParsingResult<SingleResponse> {
        return ParsingResult<SingleResponse>(parseSingleResponse(cbs));
      }));
  // Extensions currently ignored.

  return {std::move(responses)};
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
  skipCertStatus(elem);
  auto this_update = unwrap(Asn1Utility::parseGeneralizedTime(elem));
  auto next_update = unwrap(Asn1Utility::parseOptional<Envoy::SystemTime>(
      elem, Asn1Utility::parseGeneralizedTime,
      CBS_ASN1_CONSTRUCTED | CBS_ASN1_CONTEXT_SPECIFIC | 0));
  // Extensions currently ignored.

  return {cert_id, this_update, next_update};
}

CertId Asn1OcspUtility::parseCertId(CBS& cbs) {
  // CertID ::= SEQUENCE {
  //    hashAlgorithm       AlgorithmIdentifier,
  //    issuerNameHash      OCTET STRING, -- Hash of issuer's `DN`
  //    issuerKeyHash       OCTET STRING, -- Hash of issuer's public key
  //    serialNumber        CertificateSerialNumber
  // }
  CBS elem;
  if (!CBS_get_asn1(&cbs, &elem, CBS_ASN1_SEQUENCE)) {
    throw EnvoyException("OCSP CertID is not a well-formed ASN.1 SEQUENCE");
  }

  unwrap(Asn1Utility::skip(elem, CBS_ASN1_SEQUENCE));
  unwrap(Asn1Utility::skip(elem, CBS_ASN1_OCTETSTRING));
  unwrap(Asn1Utility::skip(elem, CBS_ASN1_OCTETSTRING));
  auto serial_number = unwrap(Asn1Utility::parseInteger(elem));

  return {serial_number};
}

} // namespace Ocsp
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
