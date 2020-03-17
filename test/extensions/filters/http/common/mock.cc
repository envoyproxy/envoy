#include "test/extensions/filters/http/common/mock.h"

#include <memory>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
MockUpstream::MockUpstream(Upstream::MockClusterManager& mock_cm, const std::string& status,
                           const std::string& response_body)
    : request_(&mock_cm.async_client_), status_(status), response_body_(response_body) {
  ON_CALL(mock_cm.async_client_, send_(testing::_, testing::_, testing::_))
      .WillByDefault(testing::Invoke(
          [this](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                 const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::ResponseMessagePtr response_message(
                new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", status_}}}));
            if (response_body_.length()) {
              response_message->body() = std::make_unique<Buffer::OwnedImpl>(response_body_);
            } else {
              response_message->body().reset(nullptr);
            }
            cb.onSuccess(request_, std::move(response_message));
            return &request_;
          }));
}

MockUpstream::MockUpstream(Upstream::MockClusterManager& mock_cm,
                           Http::AsyncClient::FailureReason reason)
    : request_(&mock_cm.async_client_) {
  ON_CALL(mock_cm.async_client_, send_(testing::_, testing::_, testing::_))
      .WillByDefault(testing::Invoke(
          [this, reason](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                         const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            cb.onFailure(request_, reason);
            return &request_;
          }));
}

MockUpstream::MockUpstream(Upstream::MockClusterManager& mock_cm,
                           Http::MockAsyncClientRequest* request)
    : request_(&mock_cm.async_client_) {
  ON_CALL(mock_cm.async_client_, send_(testing::_, testing::_, testing::_))
      .WillByDefault(testing::Return(request));
}
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
