#pragma once

#include "envoy/http/message.h"

#include "source/common/http/message_impl.h"
#include "source/extensions/common/aws/credentials_provider.h"
#include "source/extensions/common/aws/credentials_provider_impl.h"
#include "source/extensions/common/aws/metadata_fetcher.h"
#include "source/extensions/common/aws/signer.h"

#include "test/mocks/upstream/cluster_manager.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class MockMetadataFetcher : public MetadataFetcher {
public:
  MOCK_METHOD(void, cancel, ());
  MOCK_METHOD(absl::string_view, failureToString, (MetadataFetcher::MetadataReceiver::Failure));
  MOCK_METHOD(void, fetch,
              (Http::RequestMessage & message, Tracing::Span& parent_span,
               MetadataFetcher::MetadataReceiver& receiver));
};

class MockMetadataReceiver : public MetadataFetcher::MetadataReceiver {
public:
  MOCK_METHOD(void, onMetadataSuccess, (const std::string&& body));
  MOCK_METHOD(void, onMetadataError, (MetadataFetcher::MetadataReceiver::Failure reason));
};

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

  MOCK_METHOD(absl::Status, sign, (Http::RequestMessage&, bool, absl::string_view));
  MOCK_METHOD(absl::Status, sign, (Http::RequestHeaderMap&, const std::string&, absl::string_view));
  MOCK_METHOD(absl::Status, signEmptyPayload, (Http::RequestHeaderMap&, absl::string_view));
  MOCK_METHOD(absl::Status, signUnsignedPayload, (Http::RequestHeaderMap&, absl::string_view));
  MOCK_METHOD(absl::Status, signX509, (Http::RequestMessage&, bool, absl::string_view));
  MOCK_METHOD(absl::Status, signX509,
              (Http::RequestHeaderMap&, const std::string&, absl::string_view));
  MOCK_METHOD(absl::Status, signX509EmptyPayload, (Http::RequestHeaderMap&, absl::string_view));
  MOCK_METHOD(absl::Status, signX509UnsignedPayload, (Http::RequestHeaderMap&, absl::string_view));
};

class MockIAMRolesAnywhereCredentialsProvider : public IAMRolesAnywhereCredentialsProvider {
public:
  MockIAMRolesAnywhereCredentialsProvider();
  ~MockIAMRolesAnywhereCredentialsProvider() override;

  MOCK_METHOD(Credentials, getCredentials, ());
};

class MockIAMRolesAnywhereX509CredentialsProvider : public IAMRolesAnywhereX509CredentialsProvider {
public:
  MockIAMRolesAnywhereX509CredentialsProvider();
  ~MockIAMRolesAnywhereX509CredentialsProvider() override;

  MOCK_METHOD(X509Credentials, getCredentials, ());
};

class MockX509CredentialsProvider : public X509CredentialsProvider {
public:
  MockX509CredentialsProvider();
  ~MockX509CredentialsProvider() override;

  MOCK_METHOD(X509Credentials, getCredentials, ());
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
