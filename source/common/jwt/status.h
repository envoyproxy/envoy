#pragma once

// Copyright 2018 Google LLC
// Copyright Envoy Project Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

namespace Envoy {
namespace JwtVerify {

/**
 * Define the Jwt verification error status.
 */
enum class Status {
  Ok = 0,

  // Jwt errors:

  // Jwt missing.
  JwtMissed,

  // Jwt not valid yet.
  JwtNotYetValid,

  // Jwt expired.
  JwtExpired,

  // JWT is not in the form of Header.Payload.Signature
  JwtBadFormat,

  // Jwt header is an invalid Base64url encoded.
  JwtHeaderParseErrorBadBase64,

  // Jwt header is an invalid JSON.
  JwtHeaderParseErrorBadJson,

  // "alg" in the header is not a string.
  JwtHeaderBadAlg,

  // Value of "alg" in the header is invalid.
  JwtHeaderNotImplementedAlg,

  // "kid" in the header is not a string.
  JwtHeaderBadKid,

  // Jwt payload is an invalid Base64url encoded.
  JwtPayloadParseErrorBadBase64,

  // Jwt payload is an invalid JSON.
  JwtPayloadParseErrorBadJson,

  // Jwt payload field [iss] must be string.
  JwtPayloadParseErrorIssNotString,

  // Jwt payload field [sub] must be string.
  JwtPayloadParseErrorSubNotString,

  // Jwt payload field [iat] must be integer.
  JwtPayloadParseErrorIatNotInteger,

  // Jwt payload field [iat] must be within a 64 bit positive integer range.
  JwtPayloadParseErrorIatOutOfRange,

  // Jwt payload field [nbf] must be integer.
  JwtPayloadParseErrorNbfNotInteger,

  // Jwt payload field [nbf] must be within a 64 bit positive integer range.
  JwtPayloadParseErrorNbfOutOfRange,

  // Jwt payload field [exp] must be integer.
  JwtPayloadParseErrorExpNotInteger,

  // Jwt payload field [exp] must be within a 64 bit positive integer range.
  JwtPayloadParseErrorExpOutOfRange,

  // Jwt payload field [jti] must be string.
  JwtPayloadParseErrorJtiNotString,

  // Jwt payload field [aud] must be string or string list.
  JwtPayloadParseErrorAudNotString,

  // Jwt signature is an invalid Base64url input.
  JwtSignatureParseErrorBadBase64,

  // Jwt ED25519 signature is wrong length
  JwtEd25519SignatureWrongLength,

  // Issuer is not configured.
  JwtUnknownIssuer,

  // Audience is not allowed.
  JwtAudienceNotAllowed,

  // Jwt verification fails.
  JwtVerificationFail,

  // Found multiple Jwt tokens.
  JwtMultipleTokens,

  // Jwks errors

  // Jwks is an invalid JSON.
  JwksParseError,

  // Jwks does not have "keys".
  JwksNoKeys,

  // "keys" in Jwks is not an array.
  JwksBadKeys,

  // Jwks doesn't have any valid public key.
  JwksNoValidKeys,

  // Jwks doesn't have key to match kid or alg from Jwt.
  JwksKidAlgMismatch,

  // "n" or "e" field of a Jwk RSA is missing or has a parse error.
  JwksRsaParseError,

  // Failed to create a EC_KEY object.
  JwksEcCreateKeyFail,

  // "x" or "y" field is an invalid Base64
  JwksEcXorYBadBase64,

  // "x" or "y" field of a Jwk EC is missing or has a parse error.
  JwksEcParseError,

  // Jwks Oct key is an invalid Base64.
  JwksOctBadBase64,

  // "x" field is invalid Base64
  JwksOKPXBadBase64,
  // "x" field is wrong length
  JwksOKPXWrongLength,

  // Failed to fetch public key
  JwksFetchFail,

  // "kty" is missing in "keys".
  JwksMissingKty,
  // "kty" is not string type in "keys".
  JwksBadKty,
  // "kty" is not supported in "keys".
  JwksNotImplementedKty,

  // "alg" is not started with "RS" for a RSA key
  JwksRSAKeyBadAlg,
  // "n" field is missing for a RSA key
  JwksRSAKeyMissingN,
  // "n" field is not string for a RSA key
  JwksRSAKeyBadN,
  // "e" field is missing for a RSA key
  JwksRSAKeyMissingE,
  // "e" field is not string for a RSA key
  JwksRSAKeyBadE,

  // "alg" is not "ES256", "ES384" or "ES512" for an EC key
  JwksECKeyBadAlg,
  // "crv" field is not string for an EC key
  JwksECKeyBadCrv,
  // "crv" or "alg" is not supported for an EC key
  JwksECKeyAlgOrCrvUnsupported,
  // "crv" is not compatible with "alg" for an EC key
  JwksECKeyAlgNotCompatibleWithCrv,
  // "x" field is missing for an EC key
  JwksECKeyMissingX,
  // "x" field is not string for an EC key
  JwksECKeyBadX,
  // "y" field is missing for an EC key
  JwksECKeyMissingY,
  // "y" field is not string for an EC key
  JwksECKeyBadY,

  // "alg" is not "HS256", "HS384" or "HS512" for an HMAC key
  JwksHMACKeyBadAlg,
  // "k" field is missing for an HMAC key
  JwksHMACKeyMissingK,
  // "k" field is not string for an HMAC key
  JwksHMACKeyBadK,

  // "alg" is not "EdDSA" for an OKP key
  JwksOKPKeyBadAlg,
  // "crv" field is missing for an OKP key
  JwksOKPKeyMissingCrv,
  // "crv" field is not string for an OKP key
  JwksOKPKeyBadCrv,
  // "crv" is not supported for an OKP key
  JwksOKPKeyCrvUnsupported,
  // "x" field is missing for an OKP key
  JwksOKPKeyMissingX,
  // "x" field is not string for an OKP key
  JwksOKPKeyBadX,

  // X509 BIO_Write function fails
  JwksX509BioWriteError,
  // X509 parse pubkey fails
  JwksX509ParseError,
  // X509 get pubkey fails
  JwksX509GetPubkeyError,

  // Key type is not supported.
  JwksPemNotImplementedKty,
  // Unable to parse public key
  JwksPemBadBase64,
  // Failed to get raw ED25519 key from PEM
  JwksPemGetRawEd25519Error,

  // Failed to create BIO
  JwksBioAllocError,
};

/**
 * Convert enum status to string.
 * @param status is the enum status.
 * @return the string status.
 */
std::string getStatusString(Status status);

/**
 * Base class to keep the status that represents "OK" or the first failure.
 */
class WithStatus {
public:
  WithStatus() : status_(Status::Ok) {}

  /**
   * Get the current status.
   * @return the enum status.
   */
  Status getStatus() const { return status_; }

protected:
  void updateStatus(Status status) {
    // Only keep the first failure
    if (status_ == Status::Ok) {
      status_ = status;
    }
  }

  void resetStatus(Status status) { status_ = status; }

private:
  // The internal status.
  Status status_;
};

} // namespace JwtVerify
} // namespace Envoy
