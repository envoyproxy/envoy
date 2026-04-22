#pragma once

// Copyright 2018 Google LLC
// Copyright Envoy Project Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

#include "source/common/jwt/status.h"

#include "openssl/ec.h"
#include "openssl/evp.h"
#include "openssl/pem.h"

namespace Envoy {
namespace JwtVerify {

/**
 *  Class to parse and a hold JSON Web Key Set.
 *
 *  Usage example:
 *    JwksPtr keys = Jwks::createFrom(jwks_string, type);
 *    if (keys->getStatus() == Status::Ok) { ... }
 */
class Jwks : public WithStatus {
public:
  // Format of public key.
  enum Type { JWKS, PEM };

  // Create from string
  static std::unique_ptr<Jwks> createFrom(const std::string& pkey, Type type);
  // Executes to createFrom with type=PEM and sets additional JWKS parameters
  // not specified within the PEM.
  static std::unique_ptr<Jwks> createFromPem(const std::string& pkey, const std::string& kid,
                                             const std::string& alg);

  // Adds a key to this keyset.
  Status addKeyFromPem(const std::string& pkey, const std::string& kid, const std::string& alg);

  // Struct for JSON Web Key
  struct Pubkey {
    std::string hmac_key_;
    std::string kid_;
    std::string kty_;
    std::string alg_;
    std::string crv_;
    bssl::UniquePtr<RSA> rsa_;
    bssl::UniquePtr<EC_KEY> ec_key_;
    std::string okp_key_raw_;
    bssl::UniquePtr<BIO> bio_;
    bssl::UniquePtr<X509> x509_;
  };
  typedef std::unique_ptr<Pubkey> PubkeyPtr;

  // Access to list of Jwks
  const std::vector<PubkeyPtr>& keys() const { return keys_; }

private:
  // Create Jwks
  void createFromJwksCore(const std::string& pkey_jwks);
  // Create PEM
  void createFromPemCore(const std::string& pkey_pem);

  // List of Jwks
  std::vector<PubkeyPtr> keys_;
};

typedef std::unique_ptr<Jwks> JwksPtr;

} // namespace JwtVerify
} // namespace Envoy
