#include "envoy/extensions/filters/http/basic_auth/v3/basic_auth.pb.h"

#include "source/extensions/filters/http/basic_auth/basic_auth_filter.h"

#include "test/mocks/http/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BasicAuth {

class FilterTest : public testing::Test {
public:
  FilterTest() {
    UserMap users;
    users.insert({"user1", {"user1", "tESsBmE/yNY3lb6a0L6vVQEZNqw="}}); // user1:test1
    users.insert({"user2", {"user2", "EJ9LPFDXsN9ynSmbxvjp75Bmlx8="}}); // user2:test2
    config_ = std::make_unique<FilterConfig>(std::move(users), "x-username", "", "stats",
                                             *stats_.rootScope());
    filter_ = std::make_shared<BasicAuthFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_filter_callbacks_);
  }

  NiceMock<Stats::IsolatedStoreImpl> stats_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks_;
  FilterConfigConstSharedPtr config_;
  std::shared_ptr<BasicAuthFilter> filter_;
};

TEST_F(FilterTest, BasicAuth) {
  // user1:test1
  Http::TestRequestHeaderMapImpl request_headers_user1{{"Authorization", "Basic dXNlcjE6dGVzdDE="}};
  request_headers_user1.setScheme("http");
  request_headers_user1.setHost("host");
  request_headers_user1.setPath("/");

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(request_headers_user1, true));
  EXPECT_EQ("user1", request_headers_user1.get_("x-username"));

  // user2:test2
  Http::TestRequestHeaderMapImpl request_headers_user2{{"Authorization", "Basic dXNlcjI6dGVzdDI="}};
  request_headers_user2.setScheme("http");
  request_headers_user2.setHost("host");
  request_headers_user2.setPath("/");

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(request_headers_user2, true));
  EXPECT_EQ("user2", request_headers_user2.get_("x-username"));
}

TEST_F(FilterTest, UserNotExist) {
  // user3:test2
  Http::TestRequestHeaderMapImpl request_headers_user1{{"Authorization", "Basic dXNlcjM6dGVzdDI="}};
  request_headers_user1.setScheme("http");
  request_headers_user1.setHost("host");
  request_headers_user1.setPath("/");
  EXPECT_CALL(decoder_filter_callbacks_, requestHeaders())
      .WillOnce(testing::Return(makeOptRef(request_headers_user1)));
  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillOnce(Invoke([&](Http::Code code, absl::string_view body,
                           std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                           const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                           absl::string_view details) {
        EXPECT_EQ(Http::Code::Unauthorized, code);
        EXPECT_EQ("User authentication failed. Invalid username/password combination.", body);

        Http::TestResponseHeaderMapImpl response_headers{{":status", "401"}};
        modify_headers(response_headers);
        EXPECT_EQ(
            "Basic realm=\"http://host/\"",
            response_headers.get(Http::Headers::get().WWWAuthenticate)[0]->value().getStringView());

        EXPECT_EQ(grpc_status, absl::nullopt);
        EXPECT_EQ(details, "invalid_credential_for_basic_auth");
      }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_user1, true));
}

TEST_F(FilterTest, InvalidPassword) {
  // user1:test2
  Http::TestRequestHeaderMapImpl request_headers_user1{{"Authorization", "Basic dXNlcjE6dGVzdDI="}};
  request_headers_user1.setScheme("http");
  request_headers_user1.setHost("host");
  request_headers_user1.setPath("/");
  EXPECT_CALL(decoder_filter_callbacks_, requestHeaders())
      .WillOnce(testing::Return(makeOptRef(request_headers_user1)));

  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillOnce(Invoke([&](Http::Code code, absl::string_view body,
                           std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                           const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                           absl::string_view details) {
        EXPECT_EQ(Http::Code::Unauthorized, code);
        EXPECT_EQ("User authentication failed. Invalid username/password combination.", body);

        Http::TestResponseHeaderMapImpl response_headers{{":status", "401"}};
        modify_headers(response_headers);
        EXPECT_EQ(
            "Basic realm=\"http://host/\"",
            response_headers.get(Http::Headers::get().WWWAuthenticate)[0]->value().getStringView());

        EXPECT_EQ(grpc_status, absl::nullopt);
        EXPECT_EQ(details, "invalid_credential_for_basic_auth");
      }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_user1, true));
}

TEST_F(FilterTest, NoAuthHeader) {
  Http::TestRequestHeaderMapImpl request_headers_user1;
  request_headers_user1.setScheme("http");
  request_headers_user1.setHost("host");
  request_headers_user1.setPath("/");
  EXPECT_CALL(decoder_filter_callbacks_, requestHeaders())
      .WillOnce(testing::Return(makeOptRef(request_headers_user1)));

  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillOnce(Invoke([&](Http::Code code, absl::string_view body,
                           std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                           const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                           absl::string_view details) {
        EXPECT_EQ(Http::Code::Unauthorized, code);
        EXPECT_EQ("User authentication failed. Missing username and password.", body);

        Http::TestResponseHeaderMapImpl response_headers{{":status", "401"}};
        modify_headers(response_headers);
        EXPECT_EQ(
            "Basic realm=\"http://host/\"",
            response_headers.get(Http::Headers::get().WWWAuthenticate)[0]->value().getStringView());

        EXPECT_EQ(grpc_status, absl::nullopt);
        EXPECT_EQ(details, "no_credential_for_basic_auth");
      }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_user1, true));
}

TEST_F(FilterTest, HasAuthHeaderButNotForBasic) {
  Http::TestRequestHeaderMapImpl request_headers_user1{{"Authorization", "Bearer xxxxxxx"}};
  request_headers_user1.setScheme("http");
  request_headers_user1.setHost("host");
  request_headers_user1.setPath("/");
  EXPECT_CALL(decoder_filter_callbacks_, requestHeaders())
      .WillOnce(testing::Return(makeOptRef(request_headers_user1)));

  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillOnce(Invoke([&](Http::Code code, absl::string_view body,
                           std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                           const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                           absl::string_view details) {
        EXPECT_EQ(Http::Code::Unauthorized, code);
        EXPECT_EQ("User authentication failed. Expected 'Basic' authentication scheme.", body);

        Http::TestResponseHeaderMapImpl response_headers{{":status", "401"}};
        modify_headers(response_headers);
        EXPECT_EQ(
            "Basic realm=\"http://host/\"",
            response_headers.get(Http::Headers::get().WWWAuthenticate)[0]->value().getStringView());

        EXPECT_EQ(grpc_status, absl::nullopt);
        EXPECT_EQ(details, "invalid_scheme_for_basic_auth");
      }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_user1, true));
}

TEST_F(FilterTest, HasAuthHeaderButNoColon) {
  Http::TestRequestHeaderMapImpl request_headers_user1{{"Authorization", "Basic dXNlcjE="}};
  request_headers_user1.setScheme("http");
  request_headers_user1.setHost("host");
  request_headers_user1.setPath("/");
  EXPECT_CALL(decoder_filter_callbacks_, requestHeaders())
      .WillOnce(testing::Return(makeOptRef(request_headers_user1)));

  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillOnce(Invoke([&](Http::Code code, absl::string_view body,
                           std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                           const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                           absl::string_view details) {
        EXPECT_EQ(Http::Code::Unauthorized, code);
        EXPECT_EQ("User authentication failed. Invalid basic credential format.", body);

        Http::TestResponseHeaderMapImpl response_headers{{":status", "401"}};
        modify_headers(response_headers);
        EXPECT_EQ(
            "Basic realm=\"http://host/\"",
            response_headers.get(Http::Headers::get().WWWAuthenticate)[0]->value().getStringView());

        EXPECT_EQ(grpc_status, absl::nullopt);
        EXPECT_EQ(details, "invalid_format_for_basic_auth");
      }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_user1, true));
}

TEST_F(FilterTest, ExistingUsernameHeader) {
  // user1:test1
  Http::TestRequestHeaderMapImpl request_headers_user1{{"Authorization", "Basic dXNlcjE6dGVzdDE="},
                                                       {"x-username", "existingUsername"}};
  request_headers_user1.setScheme("http");
  request_headers_user1.setHost("host");
  request_headers_user1.setPath("/");

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(request_headers_user1, true));
  EXPECT_EQ("user1", request_headers_user1.get_("x-username"));
}

TEST_F(FilterTest, BasicAuthPerRouteDefaultSettings) {
  Http::TestRequestHeaderMapImpl empty_request_headers;
  empty_request_headers.setScheme("http");
  empty_request_headers.setHost("host");
  empty_request_headers.setPath("/");
  EXPECT_CALL(decoder_filter_callbacks_, requestHeaders())
      .WillOnce(testing::Return(makeOptRef(empty_request_headers)));
  UserMap empty_users;
  FilterConfigPerRoute basic_auth_per_route(std::move(empty_users));

  ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(testing::Return(&basic_auth_per_route));

  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillOnce(Invoke([&](Http::Code code, absl::string_view body,
                           std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                           const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                           absl::string_view details) {
        EXPECT_EQ(Http::Code::Unauthorized, code);
        EXPECT_EQ("User authentication failed. Missing username and password.", body);

        Http::TestResponseHeaderMapImpl response_headers{{":status", "401"}};
        modify_headers(response_headers);
        EXPECT_EQ(
            "Basic realm=\"http://host/\"",
            response_headers.get(Http::Headers::get().WWWAuthenticate)[0]->value().getStringView());

        EXPECT_EQ(grpc_status, absl::nullopt);
        EXPECT_EQ(details, "no_credential_for_basic_auth");
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(empty_request_headers, true));
}

TEST_F(FilterTest, BasicAuthPerRouteEnabled) {
  UserMap users_for_route;
  users_for_route.insert({"admin", {"admin", "0DPiKuNIrrVmD8IUCuw1hQxNqZc="}}); // admin:admin
  FilterConfigPerRoute basic_auth_per_route(std::move(users_for_route));

  ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(testing::Return(&basic_auth_per_route));

  Http::TestRequestHeaderMapImpl valid_credentials{{"Authorization", "Basic YWRtaW46YWRtaW4="}};
  valid_credentials.setScheme("http");
  valid_credentials.setHost("host");
  valid_credentials.setPath("/");

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(valid_credentials, true));

  Http::TestRequestHeaderMapImpl invalid_credentials{{"Authorization", "Basic dXNlcjE6dGVzdDE="}};
  invalid_credentials.setScheme("http");
  invalid_credentials.setHost("host");
  invalid_credentials.setPath("/");
  EXPECT_CALL(decoder_filter_callbacks_, requestHeaders())
      .WillOnce(testing::Return(makeOptRef(invalid_credentials)));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(invalid_credentials, true));
}

TEST_F(FilterTest, OverrideAuthorizationHeaderProvided) {
  UserMap users;
  users.insert({"user1", {"user1", "tESsBmE/yNY3lb6a0L6vVQEZNqw="}}); // user1:test1

  FilterConfigConstSharedPtr config = std::make_unique<FilterConfig>(
      std::move(users), "x-username", "x-authorization-override", "stats", *stats_.rootScope());
  std::shared_ptr<BasicAuthFilter> filter = std::make_shared<BasicAuthFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks_);

  Http::TestRequestHeaderMapImpl request_headers_user1{
      {"x-authorization-override", "Basic dXNlcjE6dGVzdDE="}};
  request_headers_user1.setScheme("http");
  request_headers_user1.setHost("host");
  request_headers_user1.setPath("/");

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter->decodeHeaders(request_headers_user1, true));
  EXPECT_EQ("user1", request_headers_user1.get_("x-username"));
}

} // namespace BasicAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
