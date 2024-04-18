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
    config_ = std::make_unique<FilterConfig>(std::move(users), "x-username", "stats",
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

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(request_headers_user1, true));
  EXPECT_EQ("user1", request_headers_user1.get_("x-username"));

  // user2:test2
  Http::TestRequestHeaderMapImpl request_headers_user2{{"Authorization", "Basic dXNlcjI6dGVzdDI="}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(request_headers_user2, true));
  EXPECT_EQ("user2", request_headers_user2.get_("x-username"));
}

TEST_F(FilterTest, UserNotExist) {
  // user3:test2
  Http::TestRequestHeaderMapImpl request_headers_user1{{"Authorization", "Basic dXNlcjM6dGVzdDI="}};

  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillOnce(Invoke([&](Http::Code code, absl::string_view body,
                           std::function<void(Http::ResponseHeaderMap & headers)>,
                           const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                           absl::string_view details) {
        EXPECT_EQ(Http::Code::Unauthorized, code);
        EXPECT_EQ("User authentication failed. Invalid username/password combination.", body);
        EXPECT_EQ(grpc_status, absl::nullopt);
        EXPECT_EQ(details, "invalid_credential_for_basic_auth");
      }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_user1, true));
}

TEST_F(FilterTest, InvalidPassword) {
  // user1:test2
  Http::TestRequestHeaderMapImpl request_headers_user1{{"Authorization", "Basic dXNlcjE6dGVzdDI="}};

  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillOnce(Invoke([&](Http::Code code, absl::string_view body,
                           std::function<void(Http::ResponseHeaderMap & headers)>,
                           const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                           absl::string_view details) {
        EXPECT_EQ(Http::Code::Unauthorized, code);
        EXPECT_EQ("User authentication failed. Invalid username/password combination.", body);
        EXPECT_EQ(grpc_status, absl::nullopt);
        EXPECT_EQ(details, "invalid_credential_for_basic_auth");
      }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_user1, true));
}

TEST_F(FilterTest, NoAuthHeader) {
  Http::TestRequestHeaderMapImpl request_headers_user1;

  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillOnce(Invoke([&](Http::Code code, absl::string_view body,
                           std::function<void(Http::ResponseHeaderMap & headers)>,
                           const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                           absl::string_view details) {
        EXPECT_EQ(Http::Code::Unauthorized, code);
        EXPECT_EQ("User authentication failed. Missing username and password.", body);
        EXPECT_EQ(grpc_status, absl::nullopt);
        EXPECT_EQ(details, "no_credential_for_basic_auth");
      }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_user1, true));
}

TEST_F(FilterTest, HasAuthHeaderButNotForBasic) {
  Http::TestRequestHeaderMapImpl request_headers_user1{{"Authorization", "Bearer xxxxxxx"}};

  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillOnce(Invoke([&](Http::Code code, absl::string_view body,
                           std::function<void(Http::ResponseHeaderMap & headers)>,
                           const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                           absl::string_view details) {
        EXPECT_EQ(Http::Code::Unauthorized, code);
        EXPECT_EQ("User authentication failed. Expected 'Basic' authentication scheme.", body);
        EXPECT_EQ(grpc_status, absl::nullopt);
        EXPECT_EQ(details, "invalid_scheme_for_basic_auth");
      }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_user1, true));
}

TEST_F(FilterTest, HasAuthHeaderButNoColon) {
  Http::TestRequestHeaderMapImpl request_headers_user1{{"Authorization", "Basic dXNlcjE="}};

  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillOnce(Invoke([&](Http::Code code, absl::string_view body,
                           std::function<void(Http::ResponseHeaderMap & headers)>,
                           const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                           absl::string_view details) {
        EXPECT_EQ(Http::Code::Unauthorized, code);
        EXPECT_EQ("User authentication failed. Invalid basic credential format.", body);
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

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(request_headers_user1, true));
  EXPECT_EQ("user1", request_headers_user1.get_("x-username"));
}

TEST_F(FilterTest, BasicAuthPerRouteDefaultSettings) {
  Http::TestRequestHeaderMapImpl empty_request_headers;
  UserMap empty_users;
  FilterConfigPerRoute basic_auth_per_route(std::move(empty_users));

  ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(testing::Return(&basic_auth_per_route));

  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillOnce(Invoke([&](Http::Code code, absl::string_view body,
                           std::function<void(Http::ResponseHeaderMap & headers)>,
                           const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                           absl::string_view details) {
        EXPECT_EQ(Http::Code::Unauthorized, code);
        EXPECT_EQ("User authentication failed. Missing username and password.", body);
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

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(valid_credentials, true));

  Http::TestRequestHeaderMapImpl invalid_credentials{{"Authorization", "Basic dXNlcjE6dGVzdDE="}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(invalid_credentials, true));
}

} // namespace BasicAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
