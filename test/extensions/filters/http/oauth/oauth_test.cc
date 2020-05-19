#include <memory>
#include <string>

#include "common/http/message_impl.h"

#include "extensions/filters/http/oauth/oauth.h"
#include "extensions/filters/http/oauth/oauth_client.h"

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
namespace Oauth {

using testing::_;
using testing::Invoke;
using testing::NiceMock;

class MockCallbacks : public OAuth2FilterCallbacks {
public:
  MOCK_METHOD(void, sendUnauthorizedResponse, ());
  MOCK_METHOD(void, onGetIdentitySuccess, (const std::string&));
  MOCK_METHOD(void, onGetAccessTokenSuccess, (const std::string&, const std::string&));
};

class OAuth2ClientTest : public testing::Test {
public:
  OAuth2ClientTest() : mock_callbacks_(new MockCallbacks), request_(&cm_.async_client_) {
    client_ = std::make_shared<OAuth2ClientImpl>(cm_, "auth", std::chrono::milliseconds(3000));
  }

  Http::AsyncClient::Callbacks* popPendingCallback() {
    if (callbacks_.empty()) {
      // Can't use ASSERT_* as this is not a test function
      throw std::underflow_error("empty deque");
    }

    auto callbacks = callbacks_.front();
    callbacks_.pop_front();
    return callbacks;
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
  mock_response->body() = std::make_unique<Buffer::OwnedImpl>(json);

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
  popPendingCallback()->onSuccess(request, std::move(mock_response));
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
  mock_response->body() = std::make_unique<Buffer::OwnedImpl>(json);

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
  popPendingCallback()->onSuccess(request, std::move(mock_response));
}

TEST_F(OAuth2ClientTest, RequestIdentitySuccess) {
  std::string json = R"EOF(
    {
      "user": {
        "username": "foo",
        "email": "bar"
      }
    }
    )EOF";
  Http::ResponseHeaderMapPtr mock_response_headers{new Http::TestResponseHeaderMapImpl{
      {Http::Headers::get().Status.get(), "200"},
      {Http::Headers::get().ContentType.get(), "application/json"},
  }};
  Http::ResponseMessagePtr mock_response(
      new Http::ResponseMessageImpl(std::move(mock_response_headers)));
  mock_response->body() = std::make_unique<Buffer::OwnedImpl>(json);

  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_.push_back(&cb);
            return &request_;
          }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncGetIdentity("a");
  EXPECT_EQ(1, callbacks_.size());
  EXPECT_CALL(*mock_callbacks_, onGetIdentitySuccess(_));
  Http::MockAsyncClientRequest request(&cm_.async_client_);
  popPendingCallback()->onSuccess(request, std::move(mock_response));
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
  client_->asyncGetIdentity("a");
  EXPECT_EQ(1, callbacks_.size());

  EXPECT_CALL(*mock_callbacks_, sendUnauthorizedResponse());
  Http::MockAsyncClientRequest request(&cm_.async_client_);
  popPendingCallback()->onFailure(request, Http::AsyncClient::FailureReason::Reset);
}

} // namespace Oauth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
