#include "source/extensions/filters/http/file_server/absl_status_to_http_status.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileServer {

TEST(AbslStatusToHttpStatus, Coverage) {
  EXPECT_EQ(abslStatusToHttpStatus(absl::StatusCode::kOk), Http::Code::OK);
  EXPECT_EQ(abslStatusToHttpStatus(absl::StatusCode::kCancelled), static_cast<Http::Code>(499));
  EXPECT_EQ(abslStatusToHttpStatus(absl::StatusCode::kUnknown), Http::Code::InternalServerError);
  EXPECT_EQ(abslStatusToHttpStatus(absl::StatusCode::kInvalidArgument), Http::Code::BadRequest);
  EXPECT_EQ(abslStatusToHttpStatus(absl::StatusCode::kDeadlineExceeded),
            Http::Code::GatewayTimeout);
  EXPECT_EQ(abslStatusToHttpStatus(absl::StatusCode::kNotFound), Http::Code::NotFound);
  EXPECT_EQ(abslStatusToHttpStatus(absl::StatusCode::kAlreadyExists), Http::Code::Conflict);
  EXPECT_EQ(abslStatusToHttpStatus(absl::StatusCode::kPermissionDenied), Http::Code::Forbidden);
  EXPECT_EQ(abslStatusToHttpStatus(absl::StatusCode::kResourceExhausted),
            Http::Code::TooManyRequests);
  EXPECT_EQ(abslStatusToHttpStatus(absl::StatusCode::kFailedPrecondition), Http::Code::BadRequest);
  EXPECT_EQ(abslStatusToHttpStatus(absl::StatusCode::kAborted), Http::Code::Conflict);
  EXPECT_EQ(abslStatusToHttpStatus(absl::StatusCode::kOutOfRange), Http::Code::RangeNotSatisfiable);
  EXPECT_EQ(abslStatusToHttpStatus(absl::StatusCode::kUnimplemented),
            Http::Code::ServiceUnavailable);
  EXPECT_EQ(abslStatusToHttpStatus(absl::StatusCode::kDataLoss), Http::Code::InternalServerError);
  EXPECT_EQ(abslStatusToHttpStatus(absl::StatusCode::kUnauthenticated), Http::Code::Unauthorized);
  EXPECT_EQ(abslStatusToHttpStatus(static_cast<absl::StatusCode>(99999999)),
            Http::Code::InternalServerError);
}

} // namespace FileServer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
