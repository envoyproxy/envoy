#pragma once

#include <string>

namespace Envoy {
namespace JwtAuthn {

/**
 * Define the Jwt verification error status.
 */
enum class Status {
  Ok = 0,

  // Jwt errors:

  // Jwt missing.
  JwtMissed = 1,

  // Jwt expired.
  JwtExpired = 2,

  // JWT is not in the form of Header.Payload.Signature
  JwtBadFormat = 3,

  // Jwt header is an invalid Base64url input or an invalid JSON.
  JwtHeaderParseError = 4,

  // Jwt header does not have "alg".
  JwtHeaderNoAlg = 5,

  // "alg" in the header is not a string.
  JwtHeaderBadAlg = 6,

  // Value of "alg" in the header is invalid.
  JwtHeaderNotImplementedAlg = 7,

  // "kid" in the header is not a string.
  JwtHeaderBadKid = 8,

  // Jwt payload is an invalid Base64url input or an invalid JSON.
  JwtPayloadParseError = 9,

  // Jwt signature is an invalid Base64url input.
  JwtSignatureParseError = 10,

  // Issuer is not configured.
  JwtUnknownIssuer = 11,

  // Audience is not allowed.
  JwtAudienceNotAllowed = 12,

  // Jwt verification fails.
  JwtVerificationFail = 13,

  // Jwks errors

  // Jwks is an invalid JSON.
  JwksParseError = 14,

  // Jwks does not have "keys".
  JwksNoKeys = 15,

  // "keys" in Jwks is not an array.
  JwksBadKeys = 16,

  // Jwks doesn't have any valid public key.
  JwksNoValidKeys = 17,

  // Jwks doesn't have key to match kid or alg from Jwt.
  JwksKidAlgMismatch = 18,

  // Jwks PEM public key is an invalid Base64.
  JwksPemBadBase64 = 19,

  // Jwks PEM public key parse error.
  JwksPemParseError = 19,

  // "n" or "e" field of a Jwk RSA is missing or has a parse error.
  JwksRsaParseError = 20,

  // Failed to create a EC_KEY object.
  JwksEcCreateKeyFail = 21,

  // "x" or "y" field of a Jwk EC is missing or has a parse error.
  JwksEcParseError = 22,

  // Failed to fetch public key
  JwksFetchFail = 23,
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

private:
  // The internal status.
  Status status_;
};

} // namespace JwtAuthn
} // namespace Envoy
