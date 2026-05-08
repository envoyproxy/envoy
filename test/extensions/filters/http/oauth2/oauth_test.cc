#include <memory>
#include <string>

#include "source/common/http/message_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/oauth2/oauth.h"
#include "source/extensions/filters/http/oauth2/oauth_client.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace std::chrono_literals;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

class MockCallbacks : public FilterCallbacks {
public:
  MOCK_METHOD(Http::FilterHeadersStatus, handleOAuthFailure,
              (const std::string& reason, const std::string& extra_details));
  MOCK_METHOD(void, onGetAccessTokenSuccess,
              (const std::string&, const std::string&, const std::string&, std::chrono::seconds));
  MOCK_METHOD(void, onRefreshAccessTokenSuccess,
              (const std::string&, const std::string&, const std::string&, std::chrono::seconds));
  MOCK_METHOD(Http::FilterHeadersStatus, onRefreshAccessTokenFailure, ());
};

class OAuth2ClientTest : public testing::Test {
public:
  OAuth2ClientTest()
      : mock_callbacks_(std::make_shared<MockCallbacks>()),
        request_(&cm_.thread_local_cluster_.async_client_) {
    envoy::config::core::v3::HttpUri uri;
    uri.set_cluster("auth");
    uri.set_uri("auth.com/oauth/token");
    uri.mutable_timeout()->set_seconds(1);
    cm_.initializeThreadLocalClusters({"auth"});

    client_ = std::make_shared<OAuth2ClientImpl>(cm_, uri, nullptr, 0s);
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
  std::shared_ptr<MockCallbacks> mock_callbacks_;
  Http::MockAsyncClientRequest request_;
  std::deque<Http::AsyncClient::Callbacks*> callbacks_;
  std::shared_ptr<OAuth2Client> client_;
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

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                                 const Http::AsyncClient::RequestOptions&)
                                 -> Http::AsyncClient::Request* {
        EXPECT_EQ(Http::Headers::get().MethodValues.Post,
                  message->headers().Method()->value().getStringView());
        EXPECT_EQ(Http::Headers::get().ContentTypeValues.FormUrlEncoded,
                  message->headers().ContentType()->value().getStringView());
        EXPECT_NE("", message->headers().getContentLengthValue());
        EXPECT_TRUE(
            !message->headers().get(Http::CustomHeaders::get().Accept).empty() &&
            message->headers().get(Http::CustomHeaders::get().Accept)[0]->value().getStringView() ==
                Http::Headers::get().ContentTypeValues.Json);
        callbacks_.push_back(&cb);
        return &request_;
      }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncGetAccessToken("a", "b", "c", "d", "e");
  EXPECT_EQ(1, callbacks_.size());
  EXPECT_CALL(*mock_callbacks_, onGetAccessTokenSuccess("golden ticket", _, _, 1000s));
  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  ASSERT_TRUE(popPendingCallback(
      [&](auto* callback) { callback->onSuccess(request, std::move(mock_response)); }));
}

TEST_F(OAuth2ClientTest, RequestAccessTokenMissingExpiresIn) {
  std::string json = R"EOF(
    {
      "access_token": "golden ticket"
    }
    )EOF";
  Http::ResponseHeaderMapPtr mock_response_headers{new Http::TestResponseHeaderMapImpl{
      {Http::Headers::get().Status.get(), "200"},
      {Http::Headers::get().ContentType.get(), "application/json"},
  }};
  Http::ResponseMessagePtr mock_response(
      new Http::ResponseMessageImpl(std::move(mock_response_headers)));
  mock_response->body().add(json);

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_.push_back(&cb);
            return &request_;
          }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncGetAccessToken("a", "b", "c", "d", "e");
  EXPECT_EQ(1, callbacks_.size());
  EXPECT_CALL(*mock_callbacks_, handleOAuthFailure(_, _))
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  ASSERT_TRUE(popPendingCallback(
      [&](auto* callback) { callback->onSuccess(request, std::move(mock_response)); }));
}

TEST_F(OAuth2ClientTest, RequestAccessTokenDefaultExpiresIn) {
  std::string json = R"EOF(
    {
      "access_token": "golden ticket"
    }
    )EOF";
  Http::ResponseHeaderMapPtr mock_response_headers{new Http::TestResponseHeaderMapImpl{
      {Http::Headers::get().Status.get(), "200"},
      {Http::Headers::get().ContentType.get(), "application/json"},
  }};
  Http::ResponseMessagePtr mock_response(
      new Http::ResponseMessageImpl(std::move(mock_response_headers)));
  mock_response->body().add(json);

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_.push_back(&cb);
            return &request_;
          }));

  envoy::config::core::v3::HttpUri uri;
  uri.set_cluster("auth");
  uri.set_uri("auth.com/oauth/token");
  uri.mutable_timeout()->set_seconds(1);
  client_ = std::make_shared<OAuth2ClientImpl>(cm_, uri, nullptr, 2000s);
  client_->setCallbacks(*mock_callbacks_);
  client_->asyncGetAccessToken("a", "b", "c", "d", "e");
  EXPECT_EQ(1, callbacks_.size());
  EXPECT_CALL(*mock_callbacks_, onGetAccessTokenSuccess("golden ticket", _, _, 2000s));
  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
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

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_.push_back(&cb);
            return &request_;
          }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncGetAccessToken("a", "b", "c", "d", "e");
  EXPECT_EQ(1, callbacks_.size());
  EXPECT_CALL(*mock_callbacks_, handleOAuthFailure(_, _))
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
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

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_.push_back(&cb);
            return &request_;
          }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncGetAccessToken("a", "b", "c", "d", "e");
  EXPECT_EQ(1, callbacks_.size());
  EXPECT_CALL(*mock_callbacks_, handleOAuthFailure(_, _))
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
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

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_.push_back(&cb);
            return &request_;
          }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncGetAccessToken("a", "b", "c", "d", "e");
  EXPECT_EQ(1, callbacks_.size());
  EXPECT_CALL(*mock_callbacks_, handleOAuthFailure(_, _))
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  ASSERT_TRUE(popPendingCallback(
      [&](auto* callback) { callback->onSuccess(request, std::move(mock_response)); }));
}

TEST_F(OAuth2ClientTest, RequestAccessTokenNetworkError) {
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_.push_back(&cb);
            return &request_;
          }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncGetAccessToken("a", "b", "c", "d", "e");
  EXPECT_EQ(1, callbacks_.size());

  EXPECT_CALL(*mock_callbacks_, handleOAuthFailure(_, _))
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  ASSERT_TRUE(popPendingCallback([&](auto* callback) {
    callback->onFailure(request, Http::AsyncClient::FailureReason::Reset);
  }));
}

TEST_F(OAuth2ClientTest, RequestAccessTokenUnhealthyUpstream) {
  Http::ResponseHeaderMapPtr mock_response_headers{new Http::TestResponseHeaderMapImpl{
      {Http::Headers::get().Status.get(), "503"},
  }};
  Http::ResponseMessagePtr mock_response(
      new Http::ResponseMessageImpl(std::move(mock_response_headers)));

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            // if there is no healthy upstream, the request fails immediately
            cb.onSuccess(request_, std::move(mock_response));
            return nullptr;
          }));

  client_->setCallbacks(*mock_callbacks_);
  EXPECT_CALL(*mock_callbacks_, handleOAuthFailure(_, _))
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  client_->asyncGetAccessToken("a", "b", "c", "d", "e");
  EXPECT_EQ(client_->getState(), OAuth2Client::OAuthState::FailureStop);
}

TEST_F(OAuth2ClientTest, RequestAccessTokenSyncNetworkError) {
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            // send() calls onFailure synchronously (e.g. connection pool overflow)
            cb.onFailure(request_, Http::AsyncClient::FailureReason::Reset);
            return nullptr;
          }));

  client_->setCallbacks(*mock_callbacks_);
  EXPECT_CALL(*mock_callbacks_, handleOAuthFailure(_, _))
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  client_->asyncGetAccessToken("a", "b", "c", "d", "e");
  EXPECT_EQ(client_->getState(), OAuth2Client::OAuthState::FailureStop);
}

TEST_F(OAuth2ClientTest, RequestRefreshAccessTokenSuccess) {
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

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                                 const Http::AsyncClient::RequestOptions&)
                                 -> Http::AsyncClient::Request* {
        EXPECT_EQ(Http::Headers::get().MethodValues.Post,
                  message->headers().Method()->value().getStringView());
        EXPECT_EQ(Http::Headers::get().ContentTypeValues.FormUrlEncoded,
                  message->headers().ContentType()->value().getStringView());
        EXPECT_NE("", message->headers().getContentLengthValue());
        EXPECT_TRUE(
            !message->headers().get(Http::CustomHeaders::get().Accept).empty() &&
            message->headers().get(Http::CustomHeaders::get().Accept)[0]->value().getStringView() ==
                Http::Headers::get().ContentTypeValues.Json);
        callbacks_.push_back(&cb);
        return &request_;
      }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncRefreshAccessToken("a", "b", "c");
  EXPECT_EQ(1, callbacks_.size());
  EXPECT_CALL(*mock_callbacks_, onRefreshAccessTokenSuccess(_, _, _, _));
  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  ASSERT_TRUE(popPendingCallback(
      [&](auto* callback) { callback->onSuccess(request, std::move(mock_response)); }));
}

TEST_F(OAuth2ClientTest, RequestRefreshAccessTokenSuccessBasicAuthType) {
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

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                                 const Http::AsyncClient::RequestOptions&)
                                 -> Http::AsyncClient::Request* {
        EXPECT_EQ(Http::Headers::get().MethodValues.Post,
                  message->headers().Method()->value().getStringView());
        EXPECT_EQ(Http::Headers::get().ContentTypeValues.FormUrlEncoded,
                  message->headers().ContentType()->value().getStringView());
        EXPECT_NE("", message->headers().getContentLengthValue());
        EXPECT_TRUE(
            !message->headers().get(Http::CustomHeaders::get().Accept).empty() &&
            message->headers().get(Http::CustomHeaders::get().Accept)[0]->value().getStringView() ==
                Http::Headers::get().ContentTypeValues.Json);
        callbacks_.push_back(&cb);
        return &request_;
      }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncRefreshAccessToken("a", "b", "c", AuthType::BasicAuth);
  EXPECT_EQ(1, callbacks_.size());
  EXPECT_CALL(*mock_callbacks_, onRefreshAccessTokenSuccess(_, _, _, _));
  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  ASSERT_TRUE(popPendingCallback(
      [&](auto* callback) { callback->onSuccess(request, std::move(mock_response)); }));
}

TEST_F(OAuth2ClientTest, RequestAccessTokenTlsClientAuthNoClientSecret) {
  EXPECT_CALL(request_, cancel()).Times(testing::AnyNumber());
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            const std::string body = message->body().toString();
            EXPECT_EQ(std::string::npos, body.find("client_secret="));
            EXPECT_NE(std::string::npos, body.find("client_id=client_id"));
            EXPECT_TRUE(message->headers().get(Http::CustomHeaders::get().Authorization).empty());
            callbacks_.push_back(&cb);
            return &request_;
          }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncGetAccessToken("auth_code", "client_id", "secret", "cb", "verifier",
                               AuthType::TlsClientAuth);
  EXPECT_EQ(1, callbacks_.size());
}

TEST_F(OAuth2ClientTest, RequestRefreshAccessTokenTlsClientAuthNoClientSecret) {
  EXPECT_CALL(request_, cancel()).Times(testing::AnyNumber());
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            const std::string body = message->body().toString();
            EXPECT_EQ(std::string::npos, body.find("client_secret="));
            EXPECT_NE(std::string::npos, body.find("client_id=client_id"));
            EXPECT_TRUE(message->headers().get(Http::CustomHeaders::get().Authorization).empty());
            callbacks_.push_back(&cb);
            return &request_;
          }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncRefreshAccessToken("refresh", "client_id", "secret", AuthType::TlsClientAuth);
  EXPECT_EQ(1, callbacks_.size());
}

TEST_F(OAuth2ClientTest, RequestRefreshAccessTokenErrorResponse) {
  Http::ResponseHeaderMapPtr mock_response_headers{new Http::TestResponseHeaderMapImpl{
      {Http::Headers::get().Status.get(), "500"},
      {Http::Headers::get().ContentType.get(), "application/json"},
  }};
  Http::ResponseMessagePtr mock_response(
      new Http::ResponseMessageImpl(std::move(mock_response_headers)));

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_.push_back(&cb);
            return &request_;
          }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncRefreshAccessToken("a", "b", "c");
  EXPECT_EQ(1, callbacks_.size());
  EXPECT_CALL(*mock_callbacks_, onRefreshAccessTokenFailure())
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  ASSERT_TRUE(popPendingCallback(
      [&](auto* callback) { callback->onSuccess(request, std::move(mock_response)); }));
}

TEST_F(OAuth2ClientTest, RequestRefreshAccessTokenNetworkError) {
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_.push_back(&cb);
            return &request_;
          }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncRefreshAccessToken("a", "b", "c");
  EXPECT_EQ(1, callbacks_.size());

  EXPECT_CALL(*mock_callbacks_, onRefreshAccessTokenFailure())
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  ASSERT_TRUE(popPendingCallback([&](auto* callback) {
    callback->onFailure(request, Http::AsyncClient::FailureReason::Reset);
  }));
}

TEST_F(OAuth2ClientTest, RequestRefreshAccessTokenUnhealthyUpstream) {
  Http::ResponseHeaderMapPtr mock_response_headers{new Http::TestResponseHeaderMapImpl{
      {Http::Headers::get().Status.get(), "503"},
  }};
  Http::ResponseMessagePtr mock_response(
      new Http::ResponseMessageImpl(std::move(mock_response_headers)));

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            // if there is no healthy upstream, the request fails immediately
            cb.onSuccess(request_, std::move(mock_response));
            return nullptr;
          }));

  client_->setCallbacks(*mock_callbacks_);
  EXPECT_CALL(*mock_callbacks_, onRefreshAccessTokenFailure())
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  client_->asyncRefreshAccessToken("a", "b", "c");
  EXPECT_EQ(client_->getState(), OAuth2Client::OAuthState::FailureStop);
}

TEST_F(OAuth2ClientTest, RequestRefreshAccessTokenUnhealthyUpstreamAllowFailed) {
  Http::ResponseHeaderMapPtr mock_response_headers{new Http::TestResponseHeaderMapImpl{
      {Http::Headers::get().Status.get(), "503"},
  }};
  Http::ResponseMessagePtr mock_response(
      new Http::ResponseMessageImpl(std::move(mock_response_headers)));

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            cb.onSuccess(request_, std::move(mock_response));
            return nullptr;
          }));

  client_->setCallbacks(*mock_callbacks_);
  EXPECT_CALL(*mock_callbacks_, onRefreshAccessTokenFailure())
      .WillOnce(Return(Http::FilterHeadersStatus::Continue));
  client_->asyncRefreshAccessToken("a", "b", "c");
  EXPECT_EQ(client_->getState(), OAuth2Client::OAuthState::FailureContinue);
}

TEST_F(OAuth2ClientTest, RequestRefreshAccessTokenSyncNetworkError) {
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            // send() calls onFailure synchronously (e.g. connection pool overflow)
            cb.onFailure(request_, Http::AsyncClient::FailureReason::Reset);
            return nullptr;
          }));

  client_->setCallbacks(*mock_callbacks_);
  EXPECT_CALL(*mock_callbacks_, onRefreshAccessTokenFailure())
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  client_->asyncRefreshAccessToken("a", "b", "c");
  EXPECT_EQ(client_->getState(), OAuth2Client::OAuthState::FailureStop);
}

TEST_F(OAuth2ClientTest, RequestRefreshAccessTokenSyncNetworkErrorAllowFailed) {
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            cb.onFailure(request_, Http::AsyncClient::FailureReason::Reset);
            return nullptr;
          }));

  client_->setCallbacks(*mock_callbacks_);
  EXPECT_CALL(*mock_callbacks_, onRefreshAccessTokenFailure())
      .WillOnce(Return(Http::FilterHeadersStatus::Continue));
  client_->asyncRefreshAccessToken("a", "b", "c");
  EXPECT_EQ(client_->getState(), OAuth2Client::OAuthState::FailureContinue);
}

// Tests that when an async refresh token failure occurs on the allow-failed path,
// handleRefreshTokenFailure calls continueDecoding() since the request was already dispatched.
TEST_F(OAuth2ClientTest, RequestRefreshAccessTokenNetworkErrorAllowFailedContinueDecoding) {
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_.push_back(&cb);
            return &request_;
          }));

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  client_->setDecoderFilterCallbacks(decoder_callbacks);
  client_->setCallbacks(*mock_callbacks_);
  client_->asyncRefreshAccessToken("a", "b", "c");
  EXPECT_EQ(1, callbacks_.size());

  EXPECT_CALL(*mock_callbacks_, onRefreshAccessTokenFailure())
      .WillOnce(Return(Http::FilterHeadersStatus::Continue));
  EXPECT_CALL(decoder_callbacks, continueDecoding());
  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  ASSERT_TRUE(popPendingCallback([&](auto* callback) {
    callback->onFailure(request, Http::AsyncClient::FailureReason::Reset);
  }));
}

TEST_F(OAuth2ClientTest, RequestRefreshAccessTokenNetworkErrorDoubleCallStateInvalid) {
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_.push_back(&cb);
            return &request_;
          }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncRefreshAccessToken("a", "b", "c");
  EXPECT_EQ(1, callbacks_.size());

  EXPECT_CALL(*mock_callbacks_, onRefreshAccessTokenFailure())
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  ASSERT_TRUE(popPendingCallback([&](auto* callback) {
    callback->onFailure(request, Http::AsyncClient::FailureReason::Reset);
    EXPECT_DEATH(callback->onFailure(request, Http::AsyncClient::FailureReason::Reset),
                 "Malformed oauth client state");
  }));
}

TEST_F(OAuth2ClientTest, NoCluster) {
  ON_CALL(cm_, getThreadLocalCluster("auth")).WillByDefault(Return(nullptr));
  client_->setCallbacks(*mock_callbacks_);
  EXPECT_CALL(*mock_callbacks_, handleOAuthFailure(_, _))
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  client_->asyncGetAccessToken("a", "b", "c", "d", "e");
  EXPECT_EQ(client_->getState(), OAuth2Client::OAuthState::FailureStop);
  EXPECT_EQ(0, callbacks_.size());
}

TEST_F(OAuth2ClientTest, NoClusterRefreshToken) {
  ON_CALL(cm_, getThreadLocalCluster("auth")).WillByDefault(Return(nullptr));
  client_->setCallbacks(*mock_callbacks_);
  EXPECT_CALL(*mock_callbacks_, handleOAuthFailure(_, _))
      .WillOnce(Return(Http::FilterHeadersStatus::StopIteration));
  client_->asyncRefreshAccessToken("refresh", "b", "c");
  EXPECT_EQ(client_->getState(), OAuth2Client::OAuthState::FailureStop);
  EXPECT_EQ(0, callbacks_.size());
}

TEST_F(OAuth2ClientTest, NoClusterRefreshTokenAllowFailed) {
  ON_CALL(cm_, getThreadLocalCluster("auth")).WillByDefault(Return(nullptr));
  client_->setCallbacks(*mock_callbacks_);
  EXPECT_CALL(*mock_callbacks_, handleOAuthFailure(_, _))
      .WillOnce(Return(Http::FilterHeadersStatus::Continue));
  client_->asyncRefreshAccessToken("refresh", "b", "c");
  EXPECT_EQ(client_->getState(), OAuth2Client::OAuthState::FailureContinue);
  EXPECT_EQ(0, callbacks_.size());
}

TEST_F(OAuth2ClientTest, RequestAccessTokenRetryPolicy) {
  envoy::config::core::v3::HttpUri uri;
  uri.set_cluster("auth");
  uri.set_uri("auth.com/oauth/token");
  uri.mutable_timeout()->set_seconds(1);

  envoy::config::route::v3::RetryPolicy retry_policy;
  retry_policy.set_retry_on("5xx,reset");
  retry_policy.mutable_retry_back_off()->mutable_base_interval()->set_seconds(1);
  retry_policy.mutable_retry_back_off()->mutable_max_interval()->set_seconds(10);
  retry_policy.mutable_num_retries()->set_value(5);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  auto parsed_retry_policy = Router::RetryPolicyImpl::create(
      retry_policy, ProtobufMessage::getNullValidationVisitor(), server_factory_context);

  client_ =
      std::make_shared<OAuth2ClientImpl>(cm_, uri, std::move(parsed_retry_policy.value()), 2000s);

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks&,
              const Http::AsyncClient::RequestOptions& options) -> Http::AsyncClient::Request* {
            EXPECT_TRUE(options.parsed_retry_policy != nullptr);
            EXPECT_TRUE(options.buffer_body_for_retry);
            EXPECT_EQ(options.parsed_retry_policy->numRetries(), 5);
            EXPECT_TRUE(options.parsed_retry_policy->baseInterval().has_value());
            EXPECT_TRUE(options.parsed_retry_policy->maxInterval().has_value());
            EXPECT_EQ(options.parsed_retry_policy->baseInterval().value().count(), 1 * 1000);
            EXPECT_EQ(options.parsed_retry_policy->maxInterval().value().count(), 10 * 1000);
            const auto retry_on = options.parsed_retry_policy->retryOn();
            EXPECT_TRUE(retry_on & Router::RetryPolicy::RETRY_ON_5XX);
            EXPECT_TRUE(retry_on & Router::RetryPolicy::RETRY_ON_RESET);
            return nullptr;
          }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncGetAccessToken("a", "b", "c", "d", "e");
}

TEST_F(OAuth2ClientTest, TestGetAccessTokenPlusInSecret) {
  EXPECT_CALL(request_, cancel()).Times(testing::AnyNumber());
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            const std::string body = message->body().toString();
            EXPECT_NE(std::string::npos, body.find("client_secret=abc%2Bdef"));
            EXPECT_EQ(std::string::npos, body.find("client_secret=abc+def"));
            callbacks_.push_back(&cb);
            return &request_;
          }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncGetAccessToken("auth_code", "client_id", "abc+def", "http://cb", "verifier");
  EXPECT_EQ(1, callbacks_.size());
}

TEST_F(OAuth2ClientTest, TestGetAccessTokenPlusInClientId) {
  EXPECT_CALL(request_, cancel()).Times(testing::AnyNumber());
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            const std::string body = message->body().toString();
            EXPECT_NE(std::string::npos, body.find("client_id=id%2Btest"));
            EXPECT_EQ(std::string::npos, body.find("client_id=id+test"));
            callbacks_.push_back(&cb);
            return &request_;
          }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncGetAccessToken("auth_code", "id+test", "secret", "http://cb", "verifier");
  EXPECT_EQ(1, callbacks_.size());
}

TEST_F(OAuth2ClientTest, TestRefreshAccessTokenPlusInRefreshToken) {
  EXPECT_CALL(request_, cancel()).Times(testing::AnyNumber());
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            const std::string body = message->body().toString();
            EXPECT_NE(std::string::npos, body.find("refresh_token=tok%2Ben"));
            EXPECT_EQ(std::string::npos, body.find("refresh_token=tok+en"));
            callbacks_.push_back(&cb);
            return &request_;
          }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncRefreshAccessToken("tok+en", "client_id", "secret");
  EXPECT_EQ(1, callbacks_.size());
}

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
