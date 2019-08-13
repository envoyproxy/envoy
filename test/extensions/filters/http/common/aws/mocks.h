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
  ~MockCredentialsProvider() override;

  MOCK_METHOD0(getCredentials, Credentials());
};

class MockSigner : public Signer {
public:
  MockSigner();
  ~MockSigner() override;

  MOCK_METHOD2(sign, void(Http::Message&, bool));
};

class MockMetadataFetcher {
public:
  virtual ~MockMetadataFetcher() = default;

  MOCK_CONST_METHOD3(fetch, absl::optional<std::string>(const std::string&, const std::string&,
                                                        const absl::optional<std::string>&));
};

class DummyMetadataFetcher {
public:
  absl::optional<std::string> operator()(const std::string&, const std::string&,
                                         const absl::optional<std::string>&) {
    return absl::nullopt;
  }
};

} // namespace Aws
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
