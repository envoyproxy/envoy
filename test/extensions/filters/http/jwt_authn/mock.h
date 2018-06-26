#include "extensions/filters/http/jwt_authn/authenticator.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

class MockAuthenticatorCallbacks : public Authenticator::Callbacks {
public:
  MOCK_METHOD1(onComplete, void(const ::google::jwt_verify::Status& status));
};

class MockAuthenticator : public Authenticator {
public:
  MOCK_METHOD2(verify, void(Http::HeaderMap& headers, Authenticator::Callbacks* callback));
  MOCK_METHOD0(onDestroy, void());
  MOCK_CONST_METHOD1(sanitizePayloadHeaders, void(Http::HeaderMap& headers));
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
