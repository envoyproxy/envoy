#pragma once

#include "extensions/filters/http/common/aws/credentials_provider.h"
#include "extensions/filters/http/common/aws/signer.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace Aws {

class MockCredentialsProvider : public CredentialsProvider {
public:
  MockCredentialsProvider();
  ~MockCredentialsProvider();

  MOCK_METHOD0(getCredentials, Credentials());
};

class MockSigner : public Signer {
public:
  MockSigner();
  ~MockSigner();

  MOCK_METHOD2(sign, void(Http::Message&, bool));
};

} // namespace Aws
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
