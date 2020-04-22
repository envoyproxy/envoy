// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quic_cert_utils_impl.h"

#include "openssl/bytestring.h"

namespace quic {

// static
bool QuicCertUtilsImpl::ExtractSubjectNameFromDERCert(quiche::QuicheStringPiece cert,
                                                      quiche::QuicheStringPiece* subject_out) {
  CBS tbs_certificate;
  if (!SeekToSubject(cert, &tbs_certificate)) {
    return false;
  }

  CBS subject;
  if (!CBS_get_asn1_element(&tbs_certificate, &subject, CBS_ASN1_SEQUENCE)) {
    return false;
  }
  *subject_out =
      absl::string_view(reinterpret_cast<const char*>(CBS_data(&subject)), CBS_len(&subject));
  return true;
}

// static
bool QuicCertUtilsImpl::SeekToSubject(quiche::QuicheStringPiece cert, CBS* tbs_certificate) {
  CBS der;
  CBS_init(&der, reinterpret_cast<const uint8_t*>(cert.data()), cert.size());
  CBS certificate;
  // From RFC 5280, section 4.1
  //    Certificate  ::=  SEQUENCE  {
  //      tbsCertificate       TBSCertificate,
  //      signatureAlgorithm   AlgorithmIdentifier,
  //      signatureValue       BIT STRING  }

  // TBSCertificate  ::=  SEQUENCE  {
  //      version         [0]  EXPLICIT Version DEFAULT v1,
  //      serialNumber         CertificateSerialNumber,
  //      signature            AlgorithmIdentifier,
  //      issuer               Name,
  //      validity             Validity,
  //      subject              Name,
  //      subjectPublicKeyInfo SubjectPublicKeyInfo,
  if (!CBS_get_asn1(&der, &certificate, CBS_ASN1_SEQUENCE) ||
      CBS_len(&der) != 0 || // We don't allow junk after the certificate.
      !CBS_get_asn1(&certificate, tbs_certificate, CBS_ASN1_SEQUENCE) ||
      // version.
      !CBS_get_optional_asn1(tbs_certificate, nullptr, nullptr,
                             CBS_ASN1_CONSTRUCTED | CBS_ASN1_CONTEXT_SPECIFIC | 0) ||
      // Serial number.
      !CBS_get_asn1(tbs_certificate, nullptr, CBS_ASN1_INTEGER) ||
      // Signature.
      !CBS_get_asn1(tbs_certificate, nullptr, CBS_ASN1_SEQUENCE) ||
      // Issuer.
      !CBS_get_asn1(tbs_certificate, nullptr, CBS_ASN1_SEQUENCE) ||
      // Validity.
      !CBS_get_asn1(tbs_certificate, nullptr, CBS_ASN1_SEQUENCE)) {
    return false;
  }
  return true;
}

} // namespace quic
