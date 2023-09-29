#pragma once

#include "envoy/http/message.h"

#include "source/common/http/message_impl.h"
#include "source/extensions/common/aws/credentials_provider.h"
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

class DummyFetchMetadata {
public:
  absl::optional<std::string> operator()(Http::RequestMessage&) { return absl::nullopt; }
};

// A mock HTTP upstream with response body.
class MockUpstream {
public:
  /**
   * Mock upstream which returns a given response body.
   */
  MockUpstream(Upstream::MockClusterManager& mock_cm, const std::string& status,
               const std::string& response_body);
  /**
   * Mock upstream which returns a given failure.
   */
  MockUpstream(Upstream::MockClusterManager& mock_cm, Http::AsyncClient::FailureReason reason);
  /**
   * Mock upstream which returns the given request.
   */
  MockUpstream(Upstream::MockClusterManager& mock_cm, Http::MockAsyncClientRequest* request);

  int called_count() const { return called_count_; }

private:
  Http::MockAsyncClientRequest request_;
  std::string status_;
  std::string response_body_;
  int called_count_{};
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
