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
  MOCK_METHOD(void, sendUnauthorizedResponse, ());
  MOCK_METHOD(void, onGetAccessTokenSuccess,
              (const std::string&, const std::string&, const std::string&, std::chrono::seconds));
  MOCK_METHOD(void, onRefreshAccessTokenSuccess,
              (const std::string&, const std::string&, const std::string&, std::chrono::seconds));
  MOCK_METHOD(void, onRefreshAccessTokenFailure, ());
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

    client_ = std::make_shared<OAuth2ClientImpl>(cm_, uri, absl::nullopt, 0s);
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
  client_->asyncGetAccessToken("a", "b", "c", "d");
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
  client_->asyncGetAccessToken("a", "b", "c", "d");
  EXPECT_EQ(1, callbacks_.size());
  EXPECT_CALL(*mock_callbacks_, sendUnauthorizedResponse());
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
  client_ = std::make_shared<OAuth2ClientImpl>(cm_, uri, absl::nullopt, 2000s);
  client_->setCallbacks(*mock_callbacks_);
  client_->asyncGetAccessToken("a", "b", "c", "d");
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
  client_->asyncGetAccessToken("a", "b", "c", "d");
  EXPECT_EQ(1, callbacks_.size());
  EXPECT_CALL(*mock_callbacks_, sendUnauthorizedResponse());
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
  client_->asyncGetAccessToken("a", "b", "c", "d");
  EXPECT_EQ(1, callbacks_.size());
  EXPECT_CALL(*mock_callbacks_, sendUnauthorizedResponse());
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
  client_->asyncGetAccessToken("a", "b", "c", "d");
  EXPECT_EQ(1, callbacks_.size());
  EXPECT_CALL(*mock_callbacks_, sendUnauthorizedResponse());
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
  client_->asyncGetAccessToken("a", "b", "c", "d");
  EXPECT_EQ(1, callbacks_.size());

  EXPECT_CALL(*mock_callbacks_, sendUnauthorizedResponse());
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
  EXPECT_CALL(*mock_callbacks_, sendUnauthorizedResponse());
  client_->asyncGetAccessToken("a", "b", "c", "d");
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
  EXPECT_CALL(*mock_callbacks_, onRefreshAccessTokenFailure());
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

  EXPECT_CALL(*mock_callbacks_, onRefreshAccessTokenFailure());
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
  EXPECT_CALL(*mock_callbacks_, onRefreshAccessTokenFailure());
  client_->asyncRefreshAccessToken("a", "b", "c");
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

  EXPECT_CALL(*mock_callbacks_, onRefreshAccessTokenFailure());
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
  EXPECT_CALL(*mock_callbacks_, sendUnauthorizedResponse());
  client_->asyncGetAccessToken("a", "b", "c", "d");
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

  client_ = std::make_shared<OAuth2ClientImpl>(cm_, uri, retry_policy, 2000s);

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks&,
              const Http::AsyncClient::RequestOptions& options) -> Http::AsyncClient::Request* {
            EXPECT_TRUE(options.retry_policy.has_value());
            EXPECT_TRUE(options.buffer_body_for_retry);
            EXPECT_TRUE(options.retry_policy.value().has_num_retries());
            EXPECT_EQ(PROTOBUF_GET_WRAPPED_REQUIRED(options.retry_policy.value(), num_retries), 5);
            EXPECT_TRUE(options.retry_policy.value().has_retry_back_off());
            EXPECT_TRUE(options.retry_policy.value().retry_back_off().has_base_interval());
            EXPECT_EQ(PROTOBUF_GET_MS_REQUIRED(options.retry_policy.value().retry_back_off(),
                                               base_interval),
                      1 * 1000);
            EXPECT_TRUE(options.retry_policy.value().retry_back_off().has_max_interval());
            EXPECT_EQ(PROTOBUF_GET_MS_REQUIRED(options.retry_policy.value().retry_back_off(),
                                               max_interval),
                      10 * 1000);
            const std::string& retry_on = options.retry_policy.value().retry_on();
            std::set<std::string> retry_on_modes = absl::StrSplit(retry_on, ',');
            EXPECT_EQ(retry_on_modes.count("5xx"), 1);
            EXPECT_EQ(retry_on_modes.count("reset"), 1);

            return nullptr;
          }));

  client_->setCallbacks(*mock_callbacks_);
  client_->asyncGetAccessToken("a", "b", "c", "d");
}

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
