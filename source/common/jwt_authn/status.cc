#include "common/jwt_authn/status.h"

#include <map>

namespace Envoy {
namespace JwtAuthn {

std::string getStatusString(Status status) {
  static std::map<Status, std::string> table = {
      {Status::Ok, "OK"},

      {Status::JwtMissed, "Jwt missing"},
      {Status::JwtExpired, "Jwt expired"},
      {Status::JwtBadFormat, "Jwt is not in the form of Header.Payload.Signature"},
      {Status::JwtHeaderParseError, "Jwt header is an invalid Base64url input or an invalid JSON"},
      {Status::JwtHeaderNoAlg, "Jwt header does not have [alg] field"},
      {Status::JwtHeaderBadAlg, "Jwt header [alg] field is not a string"},
      {Status::JwtHeaderNotImplementedAlg, "Jwt header [alg] field value is invalid"},
      {Status::JwtHeaderBadKid, "Jwt header [kid] field is not a string"},
      {Status::JwtPayloadParseError,
       "Jwt payload is an invalid Base64url input or an invalid JSON"},
      {Status::JwtSignatureParseError, "Jwt signature is an invalid Base64url input"},
      {Status::JwtUnknownIssuer, "Jwt issuer is not configured"},
      {Status::JwtAudienceNotAllowed, "Audience in Jwt is not allowed"},
      {Status::JwtVerificationFail, "Jwt verification fails"},

      {Status::JwksParseError, "Jwks is an invalid JSON"},
      {Status::JwksNoKeys, "Jwks does not have [keys] field"},
      {Status::JwksBadKeys, "[keys] in Jwks is not an array"},
      {Status::JwksNoValidKeys, "Jwks doesn't have any valid public key"},
      {Status::JwksKidAlgMismatch, "Jwks doesn't have key to match kid or alg from Jwt"},
      {Status::JwksPemBadBase64, "Jwks PEM public key is an invalid Base64"},
      {Status::JwksPemParseError, "Jwks PEM public key parse error"},
      {Status::JwksRsaParseError, "Jwks RSA [n] or [e] field is missing or has a parse error"},
      {Status::JwksEcCreateKeyFail, "Jwks EC create key fail"},
      {Status::JwksEcParseError, "Jwks EC [x] or [y] field is missing or has a parse error."},
      {Status::JwksFetchFail, "Jwks fetch fail"},
  };
  return table[status];
}

} // namespace JwtAuthn
} // namespace Envoy
