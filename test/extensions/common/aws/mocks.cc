#include "test/extensions/common/aws/mocks.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

MockCredentialsProvider::MockCredentialsProvider() = default;

MockCredentialsProvider::~MockCredentialsProvider() = default;

MockSigner::MockSigner() = default;

MockSigner::~MockSigner() = default;

// Mock HTTP upstream with response body.
MockUpstream::MockUpstream(Upstream::MockClusterManager& mock_cm, const std::string& status,
                           const std::string& response_body)
    : request_(&mock_cm.thread_local_cluster_.async_client_), status_(status),
      response_body_(response_body) {
  ON_CALL(mock_cm.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillByDefault(
          Invoke([this](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                        const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::ResponseMessagePtr response_message(
                new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", status_}}}));
            response_message->body().add(response_body_);
            cb.onSuccess(request_, std::move(response_message));
            called_count_++;
            return &request_;
          }));
}

// Mock HTTP upstream with failure reason.
MockUpstream::MockUpstream(Upstream::MockClusterManager& mock_cm,
                           Http::AsyncClient::FailureReason reason)
    : request_(&mock_cm.thread_local_cluster_.async_client_) {
  ON_CALL(mock_cm.thread_local_cluster_.async_client_, send_(testing::_, testing::_, testing::_))
      .WillByDefault(testing::Invoke(
          [this, reason](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                         const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            cb.onFailure(request_, reason);
            return &request_;
          }));
}

// Mock HTTP upstream with request.
MockUpstream::MockUpstream(Upstream::MockClusterManager& mock_cm,
                           Http::MockAsyncClientRequest* request)
    : request_(&mock_cm.thread_local_cluster_.async_client_) {
  ON_CALL(mock_cm.thread_local_cluster_.async_client_, send_(testing::_, testing::_, testing::_))
      .WillByDefault(testing::Return(request));
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
