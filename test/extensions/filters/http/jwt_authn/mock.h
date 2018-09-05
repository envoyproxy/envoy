#include "extensions/filters/http/jwt_authn/authenticator.h"

#include "gmock/gmock.h"

using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

class MockAuthFactory : public AuthFactory {
public:
  MOCK_CONST_METHOD3(create, AuthenticatorPtr(const std::vector<std::string>&,
                                              const absl::optional<std::string>&, bool));
};

class MockAuthenticator : public Authenticator {
public:
  MOCK_METHOD3(doVerify, void(Http::HeaderMap& headers, std::vector<JwtLocationConstPtr>* tokens,
                              std::function<void(const ::google::jwt_verify::Status&)> callback));

  void verify(Http::HeaderMap& headers, std::vector<JwtLocationConstPtr>&& tokens,
              std::function<void(const ::google::jwt_verify::Status&)>&& callback) {
    doVerify(headers, &tokens, std::move(callback));
  }

  MOCK_METHOD0(onDestroy, void());
  MOCK_CONST_METHOD1(sanitizePayloadHeaders, void(Http::HeaderMap& headers));
};

class MockVerifierCallbacks : public VerifierCallbacks {
public:
  MOCK_METHOD1(onComplete, void(const Status& status));
};

class MockVerifier : public Verifier {
public:
  MOCK_METHOD1(verify, void(VerifyContext& context));
  MOCK_METHOD1(registerParent, void(Verifier* callback));
  MOCK_METHOD2(onComplete, void(const Status& status, VerifyContext& context));
};

class MockExtractor : public Extractor {
public:
  MOCK_CONST_METHOD1(extract, std::vector<JwtLocationConstPtr>(const Http::HeaderMap& headers));
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
