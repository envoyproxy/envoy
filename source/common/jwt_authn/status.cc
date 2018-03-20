#include "common/jwt_authn/status.h"

#include <map>

namespace Envoy {
namespace JwtAuthn {

std::string StatusToString(Status status) {
  static std::map<Status, std::string> table = {
      {Status::OK, "OK"},
      {Status::JWT_MISSED, "Required JWT token is missing"},
      {Status::JWT_EXPIRED, "JWT is expired"},
      {Status::JWT_BAD_FORMAT, "JWT_BAD_FORMAT"},
      {Status::JWT_HEADER_PARSE_ERROR, "JWT_HEADER_PARSE_ERROR"},
      {Status::JWT_HEADER_NO_ALG, "JWT_HEADER_NO_ALG"},
      {Status::JWT_HEADER_BAD_ALG, "JWT_HEADER_BAD_ALG"},
      {Status::JWT_SIGNATURE_PARSE_ERROR, "JWT_SIGNATURE_PARSE_ERROR"},
      {Status::JWT_INVALID_SIGNATURE, "JWT_INVALID_SIGNATURE"},
      {Status::JWT_PAYLOAD_PARSE_ERROR, "JWT_PAYLOAD_PARSE_ERROR"},
      {Status::JWT_HEADER_BAD_KID, "JWT_HEADER_BAD_KID"},
      {Status::JWT_UNKNOWN_ISSUER, "Unknown issuer"},
      {Status::JWK_PARSE_ERROR, "JWK_PARSE_ERROR"},
      {Status::JWK_NO_KEYS, "JWK_NO_KEYS"},
      {Status::JWK_BAD_KEYS, "JWK_BAD_KEYS"},
      {Status::JWK_NO_VALID_PUBKEY, "JWK_NO_VALID_PUBKEY"},
      {Status::KID_ALG_UNMATCH, "KID_ALG_UNMATCH"},
      {Status::ALG_NOT_IMPLEMENTED, "ALG_NOT_IMPLEMENTED"},
      {Status::PEM_PUBKEY_BAD_BASE64, "PEM_PUBKEY_BAD_BASE64"},
      {Status::PEM_PUBKEY_PARSE_ERROR, "PEM_PUBKEY_PARSE_ERROR"},
      {Status::JWK_RSA_PUBKEY_PARSE_ERROR, "JWK_RSA_PUBKEY_PARSE_ERROR"},
      {Status::FAILED_CREATE_EC_KEY, "FAILED_CREATE_EC_KEY"},
      {Status::JWK_EC_PUBKEY_PARSE_ERROR, "JWK_EC_PUBKEY_PARSE_ERROR"},
      {Status::FAILED_CREATE_ECDSA_SIGNATURE, "FAILED_CREATE_ECDSA_SIGNATURE"},
      {Status::AUDIENCE_NOT_ALLOWED, "Audience doesn't match"},
      {Status::FAILED_FETCH_PUBKEY, "Failed to fetch public key"},
  };
  return table[status];
}

} // namespace JwtAuthn
} // namespace Envoy
