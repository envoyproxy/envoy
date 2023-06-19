#pragma once

#include "source/extensions/common/aws/credentials_provider.h"
#include "source/extensions/common/aws/signer.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class MockCredentialsProvider : public CredentialsProvider {
public:
  MockCredentialsProvider();
  ~MockCredentialsProvider() override;

  MOCK_METHOD(Credentials, getCredentials, ());
};

class MockSigner : public Signer {
public:
  MockSigner();
  ~MockSigner() override;

  MOCK_METHOD(void, sign, (Http::RequestMessage&, bool, absl::string_view));
  MOCK_METHOD(void, sign, (Http::RequestHeaderMap&, const std::string&, absl::string_view));
  MOCK_METHOD(void, signEmptyPayload, (Http::RequestHeaderMap&, absl::string_view));
  MOCK_METHOD(void, signUnsignedPayload, (Http::RequestHeaderMap&, absl::string_view));
};

class MockFetchMetadata {
public:
  virtual ~MockFetchMetadata() = default;

  MOCK_METHOD(absl::optional<std::string>, fetch, (Http::RequestMessage&), (const));
};

class DummyMetadataFetcher {
public:
  absl::optional<std::string> operator()(Http::RequestMessage&) { return absl::nullopt; }
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
