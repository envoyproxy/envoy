#pragma once

#include "envoy/http/async_client.h"

#include "extensions/filters/http/jwt_authn/authenticator.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

class Verifier;
typedef std::unique_ptr<Verifier> VerifierPtr;

typedef std::function<AuthenticatorPtr(const std::vector<std::string>& audiences)> AuthFactory;

/**
 * Supports verification of JWTs with configured requirments.
 */
class Verifier {
public:
  virtual ~Verifier() {}
  /**
   * Object used to signal the caller of completion.
   */
  class Callbacks {
  public:
    virtual ~Callbacks() {}

    virtual void onComplete(const ::google::jwt_verify::Status& status) PURE;
  };
  // Verify all tokens on headers, and signal the caller with callback.
  virtual void verify(Http::HeaderMap& headers, Callbacks& callback) PURE;
  // close resouces in use.
  virtual void close() PURE;

  // Factory method for creating verifiers.
  static VerifierPtr
  create(const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRequirement& requirement,
         const Protobuf::Map<ProtobufTypes::String,
                             ::envoy::config::filter::http::jwt_authn::v2alpha::JwtProvider>&
             providers,
         const AuthFactory& factory);
};

// base verifier for provider_name, provider_and_audiences, and allow_missing_or_failed.
class AuthVerifier : public Verifier, public Authenticator::Callbacks {
public:
  AuthVerifier(const AuthFactory& factory) : factory_(factory) {}
  void beforeVerify(const std::vector<std::string>& audiences, Http::HeaderMap& headers,
                    Verifier::Callbacks& callback);

  void close() override;

protected:
  // The Authenticator factory.
  const AuthFactory factory_;
  // The Authenticator object.
  AuthenticatorPtr auth_;
  // The caller's callback.
  Verifier::Callbacks* callback_;
};

class ProviderNameVerifier : public AuthVerifier {
public:
  ProviderNameVerifier(
      const std::string& provider_name, const std::vector<std::string>& audiences,
      const AuthFactory& factory,
      const Protobuf::Map<ProtobufTypes::String,
                          ::envoy::config::filter::http::jwt_authn::v2alpha::JwtProvider>&
          providers);
  void verify(Http::HeaderMap& headers, Verifier::Callbacks& callback) override;
  void onComplete(const ::google::jwt_verify::Status& status) override;

private:
  const std::vector<std::string> audiences_;
  // provider token location info
  ExtractParam extract_param_;
  std::string issuer_;
};

// allow missing or failed verifier
class AllowFailedVerifier : public AuthVerifier {
public:
  AllowFailedVerifier(const AuthFactory& factory) : AuthVerifier(factory) {}

  void verify(Http::HeaderMap& headers, Verifier::Callbacks& callback) override;
  void onComplete(const ::google::jwt_verify::Status& status) override;
};

// Base verifier for requires all or any.
class GroupVerifier : public Verifier, public Verifier::Callbacks {
public:
  void verify(Http::HeaderMap& headers, Verifier::Callbacks& callback) override;
  void close() override;

protected:
  // The list of requirement verifiers
  std::vector<VerifierPtr> verifiers_;
  // Current return count
  std::size_t count_;
  // The caller's callback.
  Verifier::Callbacks* callback_;
};

// requires any verifier.
class AnyVerifier : public GroupVerifier {
public:
  AnyVerifier(
      const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRequirementOrList& or_list,
      const AuthFactory& factory,
      const Protobuf::Map<ProtobufTypes::String,
                          ::envoy::config::filter::http::jwt_authn::v2alpha::JwtProvider>&
          providers);
  void onComplete(const ::google::jwt_verify::Status& status) override;
};

// requires all verifier
class AllVerifier : public GroupVerifier {
public:
  AllVerifier(
      const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRequirementAndList& and_list,
      const AuthFactory& factory,
      const Protobuf::Map<ProtobufTypes::String,
                          ::envoy::config::filter::http::jwt_authn::v2alpha::JwtProvider>&
          providers);
  void onComplete(const ::google::jwt_verify::Status& status) override;
};

// match all, for requirement not set
class AllowAllVerifier : public Verifier {
public:
  void verify(Http::HeaderMap&, Verifier::Callbacks& callback) override {
    callback.onComplete(::google::jwt_verify::Status::Ok);
  }

  void close() override {}
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
