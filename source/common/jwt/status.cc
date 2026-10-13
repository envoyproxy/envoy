// Copyright 2018 Google LLC
// Copyright Envoy Project Authors
// SPDX-License-Identifier: Apache-2.0

#include "source/common/jwt/status.h"

#include <iostream>
#include <map>

namespace Envoy {
namespace JwtVerify {

std::string getStatusString(Status status) {
  switch (status) {
  case Status::Ok:
    return "OK";
  case Status::JwtMissed:
    return "Jwt is missing";
  case Status::JwtNotYetValid:
    return "Jwt not yet valid";
  case Status::JwtExpired:
    return "Jwt is expired";
  case Status::JwtBadFormat:
    return "Jwt is not in the form of Header.Payload.Signature with two dots "
           "and 3 sections";
  case Status::JwtHeaderParseErrorBadBase64:
    return "Jwt header is an invalid Base64url encoded";
  case Status::JwtHeaderParseErrorBadJson:
    return "Jwt header is an invalid JSON";
  case Status::JwtHeaderBadAlg:
    return "Jwt header [alg] field is required and must be a string";
  case Status::JwtHeaderNotImplementedAlg:
    return "Jwt header [alg] is not supported";
  case Status::JwtHeaderBadKid:
    return "Jwt header [kid] field is not a string";
  case Status::JwtPayloadParseErrorBadBase64:
    return "Jwt payload is an invalid Base64url encoded";
  case Status::JwtEd25519SignatureWrongLength:
    return "Jwt ED25519 signature is wrong length";
  case Status::JwtPayloadParseErrorBadJson:
    return "Jwt payload is an invalid JSON";
  case Status::JwtPayloadParseErrorIssNotString:
    return "Jwt payload [iss] field is not a string";
  case Status::JwtPayloadParseErrorSubNotString:
    return "Jwt payload [sub] field is not a string";
  case Status::JwtPayloadParseErrorIatNotInteger:
    return "Jwt payload [iat] field is not an integer";
  case Status::JwtPayloadParseErrorIatOutOfRange:
    return "Jwt payload [iat] field is not a positive 64 bit integer";
  case Status::JwtPayloadParseErrorNbfNotInteger:
    return "Jwt payload [nbf] field is not an integer";
  case Status::JwtPayloadParseErrorNbfOutOfRange:
    return "Jwt payload [nbf] field is not a positive 64 bit integer";
  case Status::JwtPayloadParseErrorExpNotInteger:
    return "Jwt payload [exp] field is not an integer";
  case Status::JwtPayloadParseErrorExpOutOfRange:
    return "Jwt payload [exp] field is not a positive 64 bit integer";
  case Status::JwtPayloadParseErrorJtiNotString:
    return "Jwt payload [jti] field is not a string";
  case Status::JwtPayloadParseErrorAudNotString:
    return "Jwt payload [aud] field is not a string or string list";
  case Status::JwtSignatureParseErrorBadBase64:
    return "Jwt signature is an invalid Base64url encoded";
  case Status::JwtUnknownIssuer:
    return "Jwt issuer is not configured";
  case Status::JwtAudienceNotAllowed:
    return "Audiences in Jwt are not allowed";
  case Status::JwtVerificationFail:
    return "Jwt verification fails";
  case Status::JwtMultipleTokens:
    return "Found multiple Jwt tokens";

  case Status::JwksParseError:
    return "Jwks is an invalid JSON";
  case Status::JwksNoKeys:
    return "Jwks does not have [keys] field";
  case Status::JwksBadKeys:
    return "[keys] in Jwks is not an array";
  case Status::JwksNoValidKeys:
    return "Jwks doesn't have any valid public key";
  case Status::JwksKidAlgMismatch:
    return "Jwks doesn't have key to match kid or alg from Jwt";
  case Status::JwksRsaParseError:
    return "Jwks RSA [n] or [e] field is missing or has a parse error";
  case Status::JwksEcCreateKeyFail:
    return "Jwks EC create key fail";
  case Status::JwksEcXorYBadBase64:
    return "Jwks EC [x] or [y] field is an invalid Base64.";
  case Status::JwksEcParseError:
    return "Jwks EC [x] and [y] fields have a parse error.";
  case Status::JwksOctBadBase64:
    return "Jwks Oct key is an invalid Base64";
  case Status::JwksOKPXBadBase64:
    return "Jwks OKP [x] field is an invalid Base64.";
  case Status::JwksOKPXWrongLength:
    return "Jwks OKP [x] field is wrong length.";
  case Status::JwksFetchFail:
    return "Jwks remote fetch is failed";

  case Status::JwksMissingKty:
    return "[kty] is missing in [keys]";
  case Status::JwksBadKty:
    return "[kty] is bad in [keys]";
  case Status::JwksNotImplementedKty:
    return "[kty] is not supported in [keys]";

  case Status::JwksRSAKeyBadAlg:
    return "[alg] is not started with [RS] or [PS] for an RSA key";
  case Status::JwksRSAKeyMissingN:
    return "[n] field is missing for a RSA key";
  case Status::JwksRSAKeyBadN:
    return "[n] field is not string for a RSA key";
  case Status::JwksRSAKeyMissingE:
    return "[e] field is missing for a RSA key";
  case Status::JwksRSAKeyBadE:
    return "[e] field is not string for a RSA key";

  case Status::JwksECKeyBadAlg:
    return "[alg] is not started with [ES] for an EC key";
  case Status::JwksECKeyBadCrv:
    return "[crv] field is not string for an EC key";
  case Status::JwksECKeyAlgOrCrvUnsupported:
    return "[crv] or [alg] field is not supported for an EC key";
  case Status::JwksECKeyAlgNotCompatibleWithCrv:
    return "[crv] field specified is not compatible with [alg] for an EC key";
  case Status::JwksECKeyMissingX:
    return "[x] field is missing for an EC key";
  case Status::JwksECKeyBadX:
    return "[x] field is not string for an EC key";
  case Status::JwksECKeyMissingY:
    return "[y] field is missing for an EC key";
  case Status::JwksECKeyBadY:
    return "[y] field is not string for an EC key";

  case Status::JwksHMACKeyBadAlg:
    return "[alg] does not start with [HS] for an HMAC key";
  case Status::JwksHMACKeyMissingK:
    return "[k] field is missing for an HMAC key";
  case Status::JwksHMACKeyBadK:
    return "[k] field is not string for an HMAC key";

  case Status::JwksOKPKeyBadAlg:
    return "[alg] is not [EdDSA] for an OKP key";
  case Status::JwksOKPKeyMissingCrv:
    return "[crv] field is missing for an OKP key";
  case Status::JwksOKPKeyBadCrv:
    return "[crv] field is not string for an OKP key";
  case Status::JwksOKPKeyCrvUnsupported:
    return "[crv] field is not supported for an OKP key";
  case Status::JwksOKPKeyMissingX:
    return "[x] field is missing for an OKP key";
  case Status::JwksOKPKeyBadX:
    return "[x] field is not string for an OKP key";

  case Status::JwksX509BioWriteError:
    return "X509 parse pubkey internal fails: memory allocation";
  case Status::JwksX509ParseError:
    return "X509 parse pubkey fails";
  case Status::JwksX509GetPubkeyError:
    return "X509 parse pubkey internal fails: get pubkey";

  case Status::JwksPemNotImplementedKty:
    return "PEM Key type is not supported";
  case Status::JwksPemBadBase64:
    return "PEM pubkey parse fails";
  case Status::JwksPemGetRawEd25519Error:
    return "PEM failed to get raw ED25519 key";

  case Status::JwksBioAllocError:
    return "Failed to create BIO due to memory allocation failure";
  };
  // Return empty string though switch-case is exhaustive. See issues/91.
  return "";
}

} // namespace JwtVerify
} // namespace Envoy
