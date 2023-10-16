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
    std::vector<User> users;
    users.push_back({"user1", "tESsBmE/yNY3lb6a0L6vVQEZNqw="}); // user1:test1
    users.push_back({"user2", "EJ9LPFDXsN9ynSmbxvjp75Bmlx8="}); // user2:test2
    config_ = std::make_shared<FilterConfig>(users, "stats", *stats_.rootScope());
    filter_ = std::make_shared<BasicAuthFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_filter_callbacks_);
  }

  NiceMock<Stats::IsolatedStoreImpl> stats_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks_;
  FilterConfigSharedPtr config_;
  std::shared_ptr<BasicAuthFilter> filter_;
};

TEST_F(FilterTest, BasicAuth) {
  // user1:test1
  Http::TestRequestHeaderMapImpl request_headers_user1{{"Authorization", "Basic dXNlcjE6dGVzdDE="}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(request_headers_user1, true));

  // user2:test2
  Http::TestRequestHeaderMapImpl request_headers_user2{{"Authorization", "Basic dXNlcjI6dGVzdDI="}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(request_headers_user2, true));
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
        EXPECT_EQ("Basic Auth failed", body);
        EXPECT_EQ(grpc_status, absl::nullopt);
        EXPECT_EQ(details, "");
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
        EXPECT_EQ("Basic Auth failed", body);
        EXPECT_EQ(grpc_status, absl::nullopt);
        EXPECT_EQ(details, "");
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
        EXPECT_EQ("Missing username or password", body);
        EXPECT_EQ(grpc_status, absl::nullopt);
        EXPECT_EQ(details, "");
      }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_user1, true));
}

} // namespace BasicAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

