#include <memory>
#include <string>

#include "common/http/message_impl.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/oauth2/oauth.h"
#include "extensions/filters/http/oauth2/oauth_client.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {

using testing::_;
using testing::Invoke;
using testing::NiceMock;

class MockCallbacks : public FilterCallbacks {
public:
  MOCK_METHOD(void, sendUnauthorizedResponse, ());
  MOCK_METHOD(void, onGetAccessTokenSuccess, (const std::string&, std::chrono::seconds));
};

class OAuth2ClientTest : public testing::Test {
public:
  OAuth2ClientTest()
      : mock_callbacks_(std::make_shared<MockCallbacks>()), request_(&cm_.async_client_) {
    envoy::config::core::v3::HttpUri uri;
    uri.set_cluster("auth");
    uri.set_uri("auth.com/oauth/token");
    uri.mutable_timeout()->set_seconds(1);
    client_ = std::make_shared<OAuth2ClientImpl>(cm_, uri);
  }

  ABSL_MUST_USE_RESULT
  AssertionResult popPendingCallback(std::function<void(Http::AsyncClient::Callbacks*)> func) {
    if (callbacks_.empty()) {
      return AssertionFailure() << "tried to pop callback from empty deque";
    }

    func(callbacks_.front());
    callbacks_.pop_front();
    return AssertionSuccess();
  }

  NiceMock<Upstream::MockClusterManager> cm_;
  std::shared_ptr<OAuth2Client> client_;
  std::shared_ptr<MockCallbacks> mock_callbacks_;
  Http::MockAsyncClientRequest request_;
  std::deque<Http::AsyncClient::Callbacks*> callbacks_;
};

TEST_F(OAuth2ClientTest, RequestAccessTokenSuccess) {
  std::string json = R"EOF(
    {
      "access_token": "golden ticket",
      "expires_in": 1000
    }
    )EOF";
  Http::ResponseHeaderMapPtr mock_response_headers{new Http::TestResponseHeaderMapImpl{
      {Http::Headers::get().Status.get(), "200"},
      {Http::Headers::get().ContentType.get(), "application/json"},
  }};
  Http::ResponseMessagePtr mock_response(
      new Http::ResponseMessageImpl(std::move(mock_response_headers)));
  mock_response->body().add(json);

  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_.push_back(&cb);
            return &request_;
          }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncGetAccessToken("a", "b", "c", "d");
  EXPECT_EQ(1, callbacks_.size());
  EXPECT_CALL(*mock_callbacks_, onGetAccessTokenSuccess(_, _));
  Http::MockAsyncClientRequest request(&cm_.async_client_);
  ASSERT_TRUE(popPendingCallback(
      [&](auto* callback) { callback->onSuccess(request, std::move(mock_response)); }));
}

TEST_F(OAuth2ClientTest, RequestAccessTokenIncompleteResponse) {
  std::string json = R"EOF(
    {
      "expires_in": 1000
    }
    )EOF";
  Http::ResponseHeaderMapPtr mock_response_headers{new Http::TestResponseHeaderMapImpl{
      {Http::Headers::get().Status.get(), "200"},
      {Http::Headers::get().ContentType.get(), "application/json"},
  }};
  Http::ResponseMessagePtr mock_response(
      new Http::ResponseMessageImpl(std::move(mock_response_headers)));
  mock_response->body().add(json);

  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_.push_back(&cb);
            return &request_;
          }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncGetAccessToken("a", "b", "c", "d");
  EXPECT_EQ(1, callbacks_.size());
  EXPECT_CALL(*mock_callbacks_, sendUnauthorizedResponse());
  Http::MockAsyncClientRequest request(&cm_.async_client_);
  ASSERT_TRUE(popPendingCallback(
      [&](auto* callback) { callback->onSuccess(request, std::move(mock_response)); }));
}

TEST_F(OAuth2ClientTest, RequestAccessTokenErrorResponse) {
  Http::ResponseHeaderMapPtr mock_response_headers{new Http::TestResponseHeaderMapImpl{
      {Http::Headers::get().Status.get(), "500"},
      {Http::Headers::get().ContentType.get(), "application/json"},
  }};
  Http::ResponseMessagePtr mock_response(
      new Http::ResponseMessageImpl(std::move(mock_response_headers)));

  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_.push_back(&cb);
            return &request_;
          }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncGetAccessToken("a", "b", "c", "d");
  EXPECT_EQ(1, callbacks_.size());
  EXPECT_CALL(*mock_callbacks_, sendUnauthorizedResponse());
  Http::MockAsyncClientRequest request(&cm_.async_client_);
  ASSERT_TRUE(popPendingCallback(
      [&](auto* callback) { callback->onSuccess(request, std::move(mock_response)); }));
}

TEST_F(OAuth2ClientTest, RequestAccessTokenInvalidResponse) {
  std::string json = R"EOF(
    {
      "expires_in": "some_string"
    }
    )EOF";
  Http::ResponseHeaderMapPtr mock_response_headers{new Http::TestResponseHeaderMapImpl{
      {Http::Headers::get().Status.get(), "200"},
      {Http::Headers::get().ContentType.get(), "application/json"},
  }};
  Http::ResponseMessagePtr mock_response(
      new Http::ResponseMessageImpl(std::move(mock_response_headers)));
  mock_response->body().add(json);

  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_.push_back(&cb);
            return &request_;
          }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncGetAccessToken("a", "b", "c", "d");
  EXPECT_EQ(1, callbacks_.size());
  EXPECT_CALL(*mock_callbacks_, sendUnauthorizedResponse());
  Http::MockAsyncClientRequest request(&cm_.async_client_);
  ASSERT_TRUE(popPendingCallback(
      [&](auto* callback) { callback->onSuccess(request, std::move(mock_response)); }));
}

TEST_F(OAuth2ClientTest, NetworkError) {
  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_.push_back(&cb);
            return &request_;
          }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncGetAccessToken("a", "b", "c", "d");
  EXPECT_EQ(1, callbacks_.size());

  EXPECT_CALL(*mock_callbacks_, sendUnauthorizedResponse());
  Http::MockAsyncClientRequest request(&cm_.async_client_);
  ASSERT_TRUE(popPendingCallback([&](auto* callback) {
    callback->onFailure(request, Http::AsyncClient::FailureReason::Reset);
  }));
}

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
