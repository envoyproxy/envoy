#pragma once

#include "common/aws/credentials_provider.h"
#include "common/aws/metadata_fetcher.h"
#include "common/aws/region_provider.h"
#include "common/aws/signer.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Aws {
namespace Auth {

class MockCredentialsProvider : public CredentialsProvider {
public:
  MockCredentialsProvider();
  ~MockCredentialsProvider();

  MOCK_METHOD0(getCredentials, Credentials());
};

class MockRegionProvider : public RegionProvider {
public:
  MockRegionProvider();
  ~MockRegionProvider();

  MOCK_METHOD0(getRegion, absl::optional<std::string>());
};

class MockSigner : public Signer {
public:
  MockSigner();
  ~MockSigner();

  MOCK_CONST_METHOD1(sign, void(Http::Message&));
};

class MockMetadataFetcher : public MetadataFetcher {
public:
  MockMetadataFetcher();
  ~MockMetadataFetcher();

  MOCK_CONST_METHOD4(getMetadata,
                     absl::optional<std::string>(Event::Dispatcher&, const std::string&,
                                                 const std::string&,
                                                 const absl::optional<std::string>&));
};

} // namespace Auth
} // namespace Aws
} // namespace Envoy
