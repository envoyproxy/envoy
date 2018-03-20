#pragma once

#include <string>

namespace Envoy {
namespace JwtAuthn {

// Defines the Jwt verification error status.
enum class Status {
  OK = 0,

  // JWT token is required.
  JWT_MISSED = 1,

  // Token expired.
  JWT_EXPIRED = 2,

  // Given JWT is not in the form of Header.Payload.Signature
  JWT_BAD_FORMAT = 3,

  // Header is an invalid Base64url input or an invalid JSON.
  JWT_HEADER_PARSE_ERROR = 4,

  // Header does not have "alg".
  JWT_HEADER_NO_ALG = 5,

  // "alg" in the header is not a string.
  JWT_HEADER_BAD_ALG = 6,

  // Signature is an invalid Base64url input.
  JWT_SIGNATURE_PARSE_ERROR = 7,

  // Signature Verification failed (= Failed in DigestVerifyFinal())
  JWT_INVALID_SIGNATURE = 8,

  // Signature is valid but payload is an invalid Base64url input or an invalid
  // JSON.
  JWT_PAYLOAD_PARSE_ERROR = 9,

  // "kid" in the JWT header is not a string.
  JWT_HEADER_BAD_KID = 10,

  // Issuer is not configured.
  JWT_UNKNOWN_ISSUER = 11,

  // JWK is an invalid JSON.
  JWK_PARSE_ERROR = 12,

  // JWK does not have "keys".
  JWK_NO_KEYS = 13,

  // "keys" in JWK is not an array.
  JWK_BAD_KEYS = 14,

  // There are no valid public key in given JWKs.
  JWK_NO_VALID_PUBKEY = 15,

  // There is no key the kid and the alg of which match those of the given JWT.
  KID_ALG_UNMATCH = 16,

  // Value of "alg" in the header is invalid.
  ALG_NOT_IMPLEMENTED = 17,

  // Given PEM formatted public key is an invalid Base64 input.
  PEM_PUBKEY_BAD_BASE64 = 18,

  // A parse error on PEM formatted public key happened.
  PEM_PUBKEY_PARSE_ERROR = 19,

  // "n" or "e" field of a JWK has a parse error or is missing.
  JWK_RSA_PUBKEY_PARSE_ERROR = 20,

  // Failed to create a EC_KEY object.
  FAILED_CREATE_EC_KEY = 21,

  // "x" or "y" field of a JWK has a parse error or is missing.
  JWK_EC_PUBKEY_PARSE_ERROR = 22,

  // Failed to create ECDSA_SIG object.
  FAILED_CREATE_ECDSA_SIGNATURE = 23,

  // Audience is not allowed.
  AUDIENCE_NOT_ALLOWED = 24,

  // Failed to fetch public key
  FAILED_FETCH_PUBKEY = 25,
};

// Convert status to string.
std::string StatusToString(Status status);

// Base class to keep the status that represents "OK" or the first failure.
class WithStatus {
public:
  WithStatus() : status_(Status::OK) {}

  // Get the current status.
  Status GetStatus() const { return status_; }

protected:
  void UpdateStatus(Status status) {
    // Only keep the first failure
    if (status_ == Status::OK) {
      status_ = status;
    }
  }

private:
  // The internal status.
  Status status_;
};

} // namespace JwtAuthn
} // namespace Envoy
