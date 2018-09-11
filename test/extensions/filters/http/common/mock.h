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
  MOCK_METHOD2(fetch, void(const ::envoy::api::v2::core::HttpUri& uri, JwksReceiver& receiver));
};

// A mock HTTP upstream with response body.
class MockUpstream {
public:
  MockUpstream(Upstream::MockClusterManager& mock_cm, const std::string& status,
               const std::string& response_body)
      : request_(&mock_cm.async_client_), status_(status), response_body_(response_body) {
    ON_CALL(mock_cm.async_client_, send_(testing::_, testing::_, testing::_))
        .WillByDefault(testing::Invoke([this](Http::MessagePtr&, Http::AsyncClient::Callbacks& cb,
                                              const absl::optional<std::chrono::milliseconds>&)
                                           -> Http::AsyncClient::Request* {
          Http::MessagePtr response_message(new Http::ResponseMessageImpl(
              Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", status_}}}));
          if (response_body_.length()) {
            response_message->body().reset(new Buffer::OwnedImpl(response_body_));
          } else {
            response_message->body().reset(nullptr);
          }
          cb.onSuccess(std::move(response_message));
          return &request_;
        }));
  }

  MockUpstream(Upstream::MockClusterManager& mock_cm, Http::AsyncClient::FailureReason reason)
      : request_(&mock_cm.async_client_) {
    ON_CALL(mock_cm.async_client_, send_(testing::_, testing::_, testing::_))
        .WillByDefault(testing::Invoke(
            [this, reason](
                Http::MessagePtr&, Http::AsyncClient::Callbacks& cb,
                const absl::optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
              cb.onFailure(reason);
              return &request_;
            }));
  }

  MockUpstream(Upstream::MockClusterManager& mock_cm, Http::MockAsyncClientRequest* request)
      : request_(&mock_cm.async_client_) {
    ON_CALL(mock_cm.async_client_, send_(testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(request));
  }

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
  void onJwksSuccess(google::jwt_verify::JwksPtr&& jwks) {
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
