#include "extensions/filters/http/jwt_authn/authenticator.h"

#include "gmock/gmock.h"

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
                              std::function<void(const ::google::jwt_verify::Status&)>* callback));

  void verify(Http::HeaderMap& headers, std::vector<JwtLocationConstPtr>&& tokens,
              std::function<void(const ::google::jwt_verify::Status&)>&& callback) {
    doVerify(headers, &tokens, &callback);
  }

  MOCK_METHOD0(onDestroy, void());
  MOCK_CONST_METHOD1(sanitizePayloadHeaders, void(Http::HeaderMap& headers));
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
