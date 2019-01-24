#include "envoy/grpc/status.h"

#include "common/grpc/status.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {

TEST(StatusTest, StatusMappingTest) {
  std::vector<std::pair<std::string, Status::GrpcStatus>> expected = {
      {"OK", Status::GrpcStatus::Ok},
      {"CANCELED", Status::GrpcStatus::Canceled},
      {"UNKNOWN", Status::GrpcStatus::Unknown},
      {"INVALID_ARGUMENT", Status::GrpcStatus::InvalidArgument},
      {"DEADLINE_EXCEEDED", Status::GrpcStatus::DeadlineExceeded},
      {"NOT_FOUND", Status::GrpcStatus::NotFound},
      {"ALREADY_EXISTS", Status::GrpcStatus::AlreadyExists},
      {"PERMISSION_DENIED", Status::GrpcStatus::PermissionDenied},
      {"RESOURCE_EXHAUSTED", Status::GrpcStatus::ResourceExhausted},
      {"FAILED_PRECONDITION", Status::GrpcStatus::FailedPrecondition},
      {"ABORTED", Status::GrpcStatus::Aborted},
      {"OUT_OF_RANGE", Status::GrpcStatus::OutOfRange},
      {"UNIMPLEMENTED", Status::GrpcStatus::Unimplemented},
      {"INTERNAL", Status::GrpcStatus::Internal},
      {"UNAVAILABLE", Status::GrpcStatus::Unavailable},
      {"DATA_LOSS", Status::GrpcStatus::DataLoss},
      {"UNAUTHENTICATED", Status::GrpcStatus::Unauthenticated},
  };

  EXPECT_FALSE(Utility::nameToGrpcStatus("NOT_A_VALID_VALUE").has_value());

  for (const auto& pair : expected) {
    const auto status = Utility::nameToGrpcStatus(pair.first);
    EXPECT_TRUE(status.has_value());
    EXPECT_EQ(status.value(), pair.second);
  }
}

} // namespace Grpc
} // namespace Envoy
