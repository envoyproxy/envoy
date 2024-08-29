#include "source/common/tls/ocsp/ocsp.h"

#include "source/common/common/utility.h"
#include "source/common/tls/ocsp/asn1_utility.h"
#include "source/common/tls/utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace Ocsp {

namespace CertUtility = Envoy::Extensions::TransportSockets::Tls::Utility;

namespace {

int attemptParseTagForErrorMessage(CBS& cbs) {
  unsigned tag = 0;
  if (!CBS_get_any_asn1_element(&cbs, nullptr, &tag, nullptr)) {
    return -1;
  }
  return tag;
}

absl::StatusOr<std::unique_ptr<OcspResponse>>
readDerEncodedOcspResponse(const std::vector<uint8_t>& der) {
  CBS cbs;
  CBS_init(&cbs, der.data(), der.size());

  auto resp = Asn1OcspUtility::parseOcspResponse(cbs);
  if (CBS_len(&cbs) != 0) {
    return absl::InvalidArgumentError("Data contained more than a single OCSP response");
  }

  return resp;
}

absl::Status skipResponderId(CBS& cbs) {
  // ResponderID ::= CHOICE {
  //    byName               [1] Name,
  //    byKey                [2] KeyHash
  // }
  //
  // KeyHash ::= OCTET STRING -- SHA-1 hash of responder's public key
  //    (excluding the tag and length fields)

  auto opt1 = Asn1Utility::getOptional(cbs, CBS_ASN1_CONSTRUCTED | CBS_ASN1_CONTEXT_SPECIFIC | 1);
  RETURN_IF_NOT_OK_REF(opt1.status());
  auto opt2 = Asn1Utility::getOptional(cbs, CBS_ASN1_CONSTRUCTED | CBS_ASN1_CONTEXT_SPECIFIC | 2);
  RETURN_IF_NOT_OK_REF(opt2.status());

  if (opt1.value() || opt2.value()) {
    return absl::OkStatus();
  }

  return absl::InvalidArgumentError(
      absl::StrCat("Unknown choice for Responder ID: ", attemptParseTagForErrorMessage(cbs)));
}

absl::Status skipCertStatus(CBS& cbs) {
  // CertStatus ::= CHOICE {
  //  good                [0] IMPLICIT NULL,
  //  revoked             [1] IMPLICIT RevokedInfo,
  //  unknown             [2] IMPLICIT UnknownInfo
  // }
  auto opt1 = Asn1Utility::getOptional(cbs, CBS_ASN1_CONTEXT_SPECIFIC | 0);
  RETURN_IF_NOT_OK_REF(opt1.status());
  auto opt2 = Asn1Utility::getOptional(cbs, CBS_ASN1_CONSTRUCTED | CBS_ASN1_CONTEXT_SPECIFIC | 1);
  RETURN_IF_NOT_OK_REF(opt2.status());
  auto opt3 = Asn1Utility::getOptional(cbs, CBS_ASN1_CONTEXT_SPECIFIC | 2);
  RETURN_IF_NOT_OK_REF(opt3.status());

  if (!(opt1.value() || opt2.value() || opt3.value())) {
    return absl::InvalidArgumentError(
        absl::StrCat("Unknown OcspCertStatus tag: ", attemptParseTagForErrorMessage(cbs)));
  }
  return absl::OkStatus();
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

absl::Status validateResponse(std::unique_ptr<OcspResponse>& response) {
  if (response->status_ != OcspResponseStatus::Successful) {
    return absl::InvalidArgumentError("OCSP response was unsuccessful");
  }

  if (response->response_ == nullptr) {
    return absl::InvalidArgumentError("OCSP response has no body");
  }

  // We only permit a 1:1 of certificate to response.
  if (response->response_->getNumCerts() != 1) {
    return absl::InvalidArgumentError("OCSP Response must be for one certificate only");
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<OcspResponseWrapperImpl>>
OcspResponseWrapperImpl::create(std::vector<uint8_t> der_response, TimeSource& time_source) {
  auto response_or_error = readDerEncodedOcspResponse(der_response);
  RETURN_IF_NOT_OK(response_or_error.status());
  RETURN_IF_NOT_OK(validateResponse(response_or_error.value()));
  return std::unique_ptr<OcspResponseWrapperImpl>{
      new OcspResponseWrapperImpl(der_response, time_source, std::move(response_or_error.value()))};
}

OcspResponseWrapperImpl::OcspResponseWrapperImpl(std::vector<uint8_t> der_response,
                                                 TimeSource& time_source,
                                                 std::unique_ptr<OcspResponse>&& response)
    : raw_bytes_(std::move(der_response)), response_(std::move(response)),
      time_source_(time_source) {
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
bool OcspResponseWrapperImpl::matchesCertificate(X509& cert) const {
  std::string cert_serial_number = CertUtility::getSerialNumberFromCertificate(cert);
  std::string resp_cert_serial_number = response_->response_->getCertSerialNumber();
  return resp_cert_serial_number == cert_serial_number;
}

bool OcspResponseWrapperImpl::isExpired() {
  auto& next_update = response_->response_->getNextUpdate();
  return next_update == absl::nullopt || next_update < time_source_.systemTime();
}

uint64_t OcspResponseWrapperImpl::secondsUntilExpiration() const {
  auto& next_update = response_->response_->getNextUpdate();
  auto now = time_source_.systemTime();
  if (!next_update || next_update.value() <= now) {
    return 0;
  }
  return std::chrono::duration_cast<std::chrono::seconds>(next_update.value() - now).count();
}

Envoy::SystemTime OcspResponseWrapperImpl::getThisUpdate() const {
  return response_->response_->getThisUpdate();
}

Envoy::SystemTime OcspResponseWrapperImpl::getNextUpdate() const {
  auto& next_update = response_->response_->getNextUpdate();
  if (next_update) {
    return *next_update;
  }

  return time_source_.systemTime();
}

absl::StatusOr<std::unique_ptr<OcspResponse>> Asn1OcspUtility::parseOcspResponse(CBS& cbs) {
  // OCSPResponse ::= SEQUENCE {
  //    responseStatus         OCSPResponseStatus,
  //    responseBytes          [0] EXPLICIT ResponseBytes OPTIONAL
  // }

  CBS elem;
  if (!CBS_get_asn1(&cbs, &elem, CBS_ASN1_SEQUENCE)) {
    return absl::InvalidArgumentError("OCSP Response is not a well-formed ASN.1 SEQUENCE");
  }

  auto status_or_error = Asn1OcspUtility::parseResponseStatus(elem);
  RETURN_IF_NOT_OK_REF(status_or_error.status());
  auto opt = Asn1Utility::getOptional(elem, CBS_ASN1_CONSTRUCTED | CBS_ASN1_CONTEXT_SPECIFIC | 0);
  RETURN_IF_NOT_OK_REF(opt.status());
  auto maybe_bytes = opt.value();
  ResponsePtr resp = nullptr;
  if (maybe_bytes) {
    auto resp_or_error = Asn1OcspUtility::parseResponseBytes(maybe_bytes.value());
    RETURN_IF_NOT_OK_REF(resp_or_error.status());
    resp = std::move(resp_or_error.value());
  }

  return std::make_unique<OcspResponse>(status_or_error.value(), std::move(resp));
}

absl::StatusOr<OcspResponseStatus> Asn1OcspUtility::parseResponseStatus(CBS& cbs) {
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
    return absl::InvalidArgumentError("OCSP ResponseStatus is not a well-formed ASN.1 ENUMERATED");
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
    return absl::InvalidArgumentError(
        absl::StrCat("Unknown OCSP Response Status variant: ", status_ordinal));
  }
}

absl::StatusOr<ResponsePtr> Asn1OcspUtility::parseResponseBytes(CBS& cbs) {
  // ResponseBytes ::=  SEQUENCE {
  //     responseType        RESPONSE.
  //                             &id ({ResponseSet}),
  //     response            OCTET STRING (CONTAINING RESPONSE.
  //                             &Type({ResponseSet}{@responseType}))
  // }
  CBS elem, response;
  if (!CBS_get_asn1(&cbs, &elem, CBS_ASN1_SEQUENCE)) {
    return absl::InvalidArgumentError("OCSP ResponseBytes is not a well-formed SEQUENCE");
  }

  auto parse_or_error = Asn1Utility::parseOid(elem);
  RETURN_IF_NOT_OK_REF(parse_or_error.status());
  auto oid_str = parse_or_error.value();
  if (!CBS_get_asn1(&elem, &response, CBS_ASN1_OCTETSTRING)) {
    return absl::InvalidArgumentError("Expected ASN.1 OCTETSTRING for response");
  }

  if (oid_str == BasicOcspResponse::OID) {
    return Asn1OcspUtility::parseBasicOcspResponse(response);
  }
  return absl::InvalidArgumentError(absl::StrCat("Unknown OCSP Response type with OID: ", oid_str));
}

absl::StatusOr<std::unique_ptr<BasicOcspResponse>>
Asn1OcspUtility::parseBasicOcspResponse(CBS& cbs) {
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
    return absl::InvalidArgumentError(
        "OCSP BasicOCSPResponse is not a wellf-formed ASN.1 SEQUENCE");
  }
  auto response_or_error = Asn1OcspUtility::parseResponseData(elem);
  RETURN_IF_NOT_OK_REF(response_or_error.status());
  // The `signatureAlgorithm` and `signature` are ignored because OCSP
  // responses are expected to be delivered from a reliable source.
  // Optional additional certs are ignored.

  return std::make_unique<BasicOcspResponse>(response_or_error.value());
}

absl::StatusOr<ResponseData> Asn1OcspUtility::parseResponseData(CBS& cbs) {
  // ResponseData ::= SEQUENCE {
  //    version              [0] EXPLICIT Version DEFAULT v1,
  //    responderID              ResponderID,
  //    producedAt               GeneralizedTime,
  //    responses                SEQUENCE OF SingleResponse,
  //    responseExtensions   [1] EXPLICIT Extensions OPTIONAL
  // }
  CBS elem;
  if (!CBS_get_asn1(&cbs, &elem, CBS_ASN1_SEQUENCE)) {
    return absl::InvalidArgumentError("OCSP ResponseData is not a well-formed ASN.1 SEQUENCE");
  }

  // only support v1, the value of v1 is 0x00
  auto version_or_error =
      Asn1Utility::getOptional(elem, CBS_ASN1_CONTEXT_SPECIFIC | CBS_ASN1_CONSTRUCTED | 0);
  RETURN_IF_NOT_OK_REF(version_or_error.status());
  auto version_cbs = version_or_error.value();
  if (version_cbs.has_value()) {
    auto version_or_error = Asn1Utility::parseInteger(*version_cbs);
    RETURN_IF_NOT_OK_REF(version_or_error.status());
    auto version = version_or_error.value();
    if (version != "00") {
      return absl::InvalidArgumentError(
          fmt::format("OCSP ResponseData version 0x{} is not supported", version));
    }
  }

  auto status = skipResponderId(elem);
  RETURN_IF_NOT_OK(status);
  RETURN_IF_NOT_OK_REF(Asn1Utility::skip(elem, CBS_ASN1_GENERALIZEDTIME).status());
  auto responses_or_error = Asn1Utility::parseSequenceOf<SingleResponse>(
      elem, [](CBS& cbs) -> absl::StatusOr<SingleResponse> { return {parseSingleResponse(cbs)}; });
  RETURN_IF_NOT_OK_REF(responses_or_error.status());
  // Extensions currently ignored.

  return {std::move(responses_or_error.value())};
}

absl::StatusOr<SingleResponse> Asn1OcspUtility::parseSingleResponse(CBS& cbs) {
  // SingleResponse ::= SEQUENCE {
  //    certID                  CertID,
  //    certStatus              CertStatus,
  //    thisUpdate              GeneralizedTime,
  //    nextUpdate          [0] EXPLICIT GeneralizedTime OPTIONAL,
  //    singleExtensions    [1] EXPLICIT Extensions OPTIONAL
  // }
  CBS elem;
  if (!CBS_get_asn1(&cbs, &elem, CBS_ASN1_SEQUENCE)) {
    return absl::InvalidArgumentError("OCSP SingleResponse is not a well-formed ASN.1 SEQUENCE");
  }

  auto id_or_error = Asn1OcspUtility::parseCertId(elem);
  RETURN_IF_NOT_OK_REF(id_or_error.status());
  RETURN_IF_NOT_OK(skipCertStatus(elem));
  auto this_update_or_error = Asn1Utility::parseGeneralizedTime(elem);
  RETURN_IF_NOT_OK_REF(this_update_or_error.status());
  auto next_update_or_error = Asn1Utility::parseOptional<Envoy::SystemTime>(
      elem, Asn1Utility::parseGeneralizedTime,
      CBS_ASN1_CONSTRUCTED | CBS_ASN1_CONTEXT_SPECIFIC | 0);
  RETURN_IF_NOT_OK_REF(next_update_or_error.status());
  // Extensions currently ignored.

  return SingleResponse{id_or_error.value(), this_update_or_error.value(),
                        next_update_or_error.value()};
}

absl::StatusOr<CertId> Asn1OcspUtility::parseCertId(CBS& cbs) {
  // CertID ::= SEQUENCE {
  //    hashAlgorithm       AlgorithmIdentifier,
  //    issuerNameHash      OCTET STRING, -- Hash of issuer's `DN`
  //    issuerKeyHash       OCTET STRING, -- Hash of issuer's public key
  //    serialNumber        CertificateSerialNumber
  // }
  CBS elem;
  if (!CBS_get_asn1(&cbs, &elem, CBS_ASN1_SEQUENCE)) {
    return absl::InvalidArgumentError("OCSP CertID is not a well-formed ASN.1 SEQUENCE");
  }

  RETURN_IF_NOT_OK_REF(Asn1Utility::skip(elem, CBS_ASN1_SEQUENCE).status());
  RETURN_IF_NOT_OK_REF(Asn1Utility::skip(elem, CBS_ASN1_OCTETSTRING).status());
  RETURN_IF_NOT_OK_REF(Asn1Utility::skip(elem, CBS_ASN1_OCTETSTRING).status());
  auto serial_number_or_error = Asn1Utility::parseInteger(elem);
  RETURN_IF_NOT_OK_REF(serial_number_or_error.status());

  return {serial_number_or_error.value()};
}

} // namespace Ocsp
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
