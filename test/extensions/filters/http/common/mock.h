#pragma once

#include "extensions/filters/http/common/jwks_fetcher.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {

class MockJwksFetcher : public JwksFetcher {
public:
  MOCK_METHOD0(cancel, void());
  MOCK_METHOD3(fetch, void(const ::envoy::api::v2::core::HttpUri& uri, Tracing::Span& parent_span,
                           JwksReceiver& receiver));
};

// A mock HTTP upstream.
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

private:
  Http::MockAsyncClientRequest request_;
  std::string status_;
  std::string response_body_;
};

class MockJwksReceiver : public JwksFetcher::JwksReceiver {
public:
  /* GoogleMock does handle r-value references hence the below construction.
   * Expectations and assertions should be made on onJwksSuccessImpl in place
   * of onJwksSuccess.
   */
  void onJwksSuccess(google::jwt_verify::JwksPtr&& jwks) override {
    ASSERT(jwks);
    onJwksSuccessImpl(*jwks.get());
  }
  MOCK_METHOD1(onJwksSuccessImpl, void(const google::jwt_verify::Jwks& jwks));
  MOCK_METHOD1(onJwksError, void(JwksFetcher::JwksReceiver::Failure reason));
};

} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
