#include "common/grpc/common.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"

#include "test/mocks/upstream/mocks.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {

TEST(GrpcCommonTest, GetGrpcStatus) {
  Http::TestHeaderMapImpl ok_trailers{{"grpc-status", "0"}};
  EXPECT_EQ(Status::Ok, Common::getGrpcStatus(ok_trailers).value());

  Http::TestHeaderMapImpl no_status_trailers{{"foo", "bar"}};
  EXPECT_FALSE(Common::getGrpcStatus(no_status_trailers));

  Http::TestHeaderMapImpl aborted_trailers{{"grpc-status", "10"}};
  EXPECT_EQ(Status::Aborted, Common::getGrpcStatus(aborted_trailers).value());

  Http::TestHeaderMapImpl unauth_trailers{{"grpc-status", "16"}};
  EXPECT_EQ(Status::Unauthenticated, Common::getGrpcStatus(unauth_trailers).value());

  Http::TestHeaderMapImpl invalid_trailers{{"grpc-status", "-1"}};
  EXPECT_EQ(Status::InvalidCode, Common::getGrpcStatus(invalid_trailers).value());
}

TEST(GrpcCommonTest, GetGrpcMessage) {
  Http::TestHeaderMapImpl empty_trailers;
  EXPECT_EQ("", Common::getGrpcMessage(empty_trailers));

  Http::TestHeaderMapImpl error_trailers{{"grpc-message", "Some error"}};
  EXPECT_EQ("Some error", Common::getGrpcMessage(error_trailers));

  Http::TestHeaderMapImpl empty_error_trailers{{"grpc-message", ""}};
  EXPECT_EQ("", Common::getGrpcMessage(empty_error_trailers));
}

TEST(GrpcCommonTest, GetGrpcTimeout) {
  Http::TestHeaderMapImpl empty_headers;
  EXPECT_EQ(std::chrono::milliseconds(0), Common::getGrpcTimeout(empty_headers));

  Http::TestHeaderMapImpl empty_grpc_timeout{{"grpc-timeout", ""}};
  EXPECT_EQ(std::chrono::milliseconds(0), Common::getGrpcTimeout(empty_grpc_timeout));

  Http::TestHeaderMapImpl missing_unit{{"grpc-timeout", "123"}};
  EXPECT_EQ(std::chrono::milliseconds(0), Common::getGrpcTimeout(missing_unit));

  Http::TestHeaderMapImpl illegal_unit{{"grpc-timeout", "123F"}};
  EXPECT_EQ(std::chrono::milliseconds(0), Common::getGrpcTimeout(illegal_unit));

  Http::TestHeaderMapImpl unit_hours{{"grpc-timeout", "1H"}};
  EXPECT_EQ(std::chrono::milliseconds(60 * 60 * 1000), Common::getGrpcTimeout(unit_hours));

  Http::TestHeaderMapImpl unit_minutes{{"grpc-timeout", "1M"}};
  EXPECT_EQ(std::chrono::milliseconds(60 * 1000), Common::getGrpcTimeout(unit_minutes));

  Http::TestHeaderMapImpl unit_seconds{{"grpc-timeout", "1S"}};
  EXPECT_EQ(std::chrono::milliseconds(1000), Common::getGrpcTimeout(unit_seconds));

  Http::TestHeaderMapImpl unit_milliseconds{{"grpc-timeout", "12345678m"}};
  EXPECT_EQ(std::chrono::milliseconds(12345678), Common::getGrpcTimeout(unit_milliseconds));

  Http::TestHeaderMapImpl unit_microseconds{{"grpc-timeout", "1000001u"}};
  EXPECT_EQ(std::chrono::milliseconds(1001), Common::getGrpcTimeout(unit_microseconds));

  Http::TestHeaderMapImpl unit_nanoseconds{{"grpc-timeout", "12345678n"}};
  EXPECT_EQ(std::chrono::milliseconds(13), Common::getGrpcTimeout(unit_nanoseconds));

  // Max 8 digits and no leading whitespace or +- signs are not enforced on decode,
  // so we don't test for them.
}

TEST(GrpcCommonTest, ToGrpcTimeout) {
  Http::HeaderString value;

  Common::toGrpcTimeout(std::chrono::milliseconds(0UL), value);
  EXPECT_STREQ("0m", value.c_str());

  Common::toGrpcTimeout(std::chrono::milliseconds(1UL), value);
  EXPECT_STREQ("1m", value.c_str());

  Common::toGrpcTimeout(std::chrono::milliseconds(100000000UL), value);
  EXPECT_STREQ("100000S", value.c_str());

  Common::toGrpcTimeout(std::chrono::milliseconds(100000000000UL), value);
  EXPECT_STREQ("1666666M", value.c_str());

  Common::toGrpcTimeout(std::chrono::milliseconds(9000000000000UL), value);
  EXPECT_STREQ("2500000H", value.c_str());

  Common::toGrpcTimeout(std::chrono::milliseconds(360000000000000UL), value);
  EXPECT_STREQ("99999999H", value.c_str());

  Common::toGrpcTimeout(std::chrono::milliseconds(UINT64_MAX), value);
  EXPECT_STREQ("99999999H", value.c_str());
}

TEST(GrpcCommonTest, ChargeStats) {
  NiceMock<Upstream::MockClusterInfo> cluster;
  Common::chargeStat(cluster, "service", "method", true);
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.success").value());
  EXPECT_EQ(0U, cluster.stats_store_.counter("grpc.service.method.failure").value());
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.total").value());

  Common::chargeStat(cluster, "service", "method", false);
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.success").value());
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.failure").value());
  EXPECT_EQ(2U, cluster.stats_store_.counter("grpc.service.method.total").value());

  Http::TestHeaderMapImpl trailers;
  Http::HeaderEntry& status = trailers.insertGrpcStatus();
  status.value("0", 1);
  Common::chargeStat(cluster, "grpc", "service", "method", &status);
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.0").value());
  EXPECT_EQ(2U, cluster.stats_store_.counter("grpc.service.method.success").value());
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.failure").value());
  EXPECT_EQ(3U, cluster.stats_store_.counter("grpc.service.method.total").value());

  status.value("1", 1);
  Common::chargeStat(cluster, "grpc", "service", "method", &status);
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.0").value());
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.1").value());
  EXPECT_EQ(2U, cluster.stats_store_.counter("grpc.service.method.success").value());
  EXPECT_EQ(2U, cluster.stats_store_.counter("grpc.service.method.failure").value());
  EXPECT_EQ(4U, cluster.stats_store_.counter("grpc.service.method.total").value());
}

TEST(GrpcCommonTest, PrepareHeaders) {
  {
    Http::MessagePtr message =
        Common::prepareHeaders("cluster", "service_name", "method_name", absl::nullopt);

    EXPECT_STREQ("POST", message->headers().Method()->value().c_str());
    EXPECT_STREQ("/service_name/method_name", message->headers().Path()->value().c_str());
    EXPECT_STREQ("cluster", message->headers().Host()->value().c_str());
    EXPECT_STREQ("application/grpc", message->headers().ContentType()->value().c_str());
  }
  {
    Http::MessagePtr message = Common::prepareHeaders("cluster", "service_name", "method_name",
                                                      absl::optional<std::chrono::milliseconds>(1));

    EXPECT_STREQ("POST", message->headers().Method()->value().c_str());
    EXPECT_STREQ("/service_name/method_name", message->headers().Path()->value().c_str());
    EXPECT_STREQ("cluster", message->headers().Host()->value().c_str());
    EXPECT_STREQ("application/grpc", message->headers().ContentType()->value().c_str());
    EXPECT_STREQ("1m", message->headers().GrpcTimeout()->value().c_str());
  }
  {
    Http::MessagePtr message = Common::prepareHeaders("cluster", "service_name", "method_name",
                                                      absl::optional<std::chrono::seconds>(1));

    EXPECT_STREQ("POST", message->headers().Method()->value().c_str());
    EXPECT_STREQ("/service_name/method_name", message->headers().Path()->value().c_str());
    EXPECT_STREQ("cluster", message->headers().Host()->value().c_str());
    EXPECT_STREQ("application/grpc", message->headers().ContentType()->value().c_str());
    EXPECT_STREQ("1000m", message->headers().GrpcTimeout()->value().c_str());
  }
  {
    Http::MessagePtr message = Common::prepareHeaders("cluster", "service_name", "method_name",
                                                      absl::optional<std::chrono::minutes>(1));

    EXPECT_STREQ("POST", message->headers().Method()->value().c_str());
    EXPECT_STREQ("/service_name/method_name", message->headers().Path()->value().c_str());
    EXPECT_STREQ("cluster", message->headers().Host()->value().c_str());
    EXPECT_STREQ("application/grpc", message->headers().ContentType()->value().c_str());
    EXPECT_STREQ("60000m", message->headers().GrpcTimeout()->value().c_str());
  }
  {
    Http::MessagePtr message = Common::prepareHeaders("cluster", "service_name", "method_name",
                                                      absl::optional<std::chrono::hours>(1));

    EXPECT_STREQ("POST", message->headers().Method()->value().c_str());
    EXPECT_STREQ("/service_name/method_name", message->headers().Path()->value().c_str());
    EXPECT_STREQ("cluster", message->headers().Host()->value().c_str());
    EXPECT_STREQ("application/grpc", message->headers().ContentType()->value().c_str());
    EXPECT_STREQ("3600000m", message->headers().GrpcTimeout()->value().c_str());
  }
  {
    Http::MessagePtr message = Common::prepareHeaders(
        "cluster", "service_name", "method_name", absl::optional<std::chrono::hours>(100000000));

    EXPECT_STREQ("POST", message->headers().Method()->value().c_str());
    EXPECT_STREQ("/service_name/method_name", message->headers().Path()->value().c_str());
    EXPECT_STREQ("cluster", message->headers().Host()->value().c_str());
    EXPECT_STREQ("application/grpc", message->headers().ContentType()->value().c_str());
    EXPECT_STREQ("99999999H", message->headers().GrpcTimeout()->value().c_str());
  }
  {
    Http::MessagePtr message =
        Common::prepareHeaders("cluster", "service_name", "method_name",
                               absl::optional<std::chrono::milliseconds>(100000000000));

    EXPECT_STREQ("POST", message->headers().Method()->value().c_str());
    EXPECT_STREQ("/service_name/method_name", message->headers().Path()->value().c_str());
    EXPECT_STREQ("cluster", message->headers().Host()->value().c_str());
    EXPECT_STREQ("application/grpc", message->headers().ContentType()->value().c_str());
    EXPECT_STREQ("1666666M", message->headers().GrpcTimeout()->value().c_str());
  }
}

TEST(GrpcCommonTest, ResolveServiceAndMethod) {
  std::string service;
  std::string method;
  Http::HeaderMapImpl headers;
  Http::HeaderEntry& path = headers.insertPath();
  path.value(std::string("/service_name/method_name"));
  EXPECT_TRUE(Common::resolveServiceAndMethod(&path, &service, &method));
  EXPECT_EQ("service_name", service);
  EXPECT_EQ("method_name", method);
  path.value(std::string(""));
  EXPECT_FALSE(Common::resolveServiceAndMethod(&path, &service, &method));
  path.value(std::string("/"));
  EXPECT_FALSE(Common::resolveServiceAndMethod(&path, &service, &method));
  path.value(std::string("//"));
  EXPECT_FALSE(Common::resolveServiceAndMethod(&path, &service, &method));
  path.value(std::string("/service_name"));
  EXPECT_FALSE(Common::resolveServiceAndMethod(&path, &service, &method));
  path.value(std::string("/service_name/"));
  EXPECT_FALSE(Common::resolveServiceAndMethod(&path, &service, &method));
}

TEST(GrpcCommonTest, GrpcToHttpStatus) {
  const std::vector<std::pair<Status::GrpcStatus, uint64_t>> test_set = {
      {Status::GrpcStatus::Ok, 200},
      {Status::GrpcStatus::Canceled, 499},
      {Status::GrpcStatus::Unknown, 500},
      {Status::GrpcStatus::InvalidArgument, 400},
      {Status::GrpcStatus::DeadlineExceeded, 504},
      {Status::GrpcStatus::NotFound, 404},
      {Status::GrpcStatus::AlreadyExists, 409},
      {Status::GrpcStatus::PermissionDenied, 403},
      {Status::GrpcStatus::ResourceExhausted, 429},
      {Status::GrpcStatus::FailedPrecondition, 400},
      {Status::GrpcStatus::Aborted, 409},
      {Status::GrpcStatus::OutOfRange, 400},
      {Status::GrpcStatus::Unimplemented, 501},
      {Status::GrpcStatus::Internal, 500},
      {Status::GrpcStatus::Unavailable, 503},
      {Status::GrpcStatus::DataLoss, 500},
      {Status::GrpcStatus::Unauthenticated, 401},
      {Status::GrpcStatus::InvalidCode, 500},
  };
  for (const auto& test_case : test_set) {
    EXPECT_EQ(test_case.second, Grpc::Utility::grpcToHttpStatus(test_case.first));
  }
}

TEST(GrpcCommonTest, HttpToGrpcStatus) {
  const std::vector<std::pair<uint64_t, Status::GrpcStatus>> test_set = {
      {400, Status::GrpcStatus::Internal},         {401, Status::GrpcStatus::Unauthenticated},
      {403, Status::GrpcStatus::PermissionDenied}, {404, Status::GrpcStatus::Unimplemented},
      {429, Status::GrpcStatus::Unavailable},      {502, Status::GrpcStatus::Unavailable},
      {503, Status::GrpcStatus::Unavailable},      {504, Status::GrpcStatus::Unavailable},
      {500, Status::GrpcStatus::Unknown},
  };
  for (const auto& test_case : test_set) {
    EXPECT_EQ(test_case.second, Grpc::Utility::httpToGrpcStatus(test_case.first));
  }
}

TEST(GrpcCommonTest, HasGrpcContentType) {
  {
    Http::TestHeaderMapImpl headers{};
    EXPECT_FALSE(Common::hasGrpcContentType(headers));
  }
  auto isGrpcContentType = [](const std::string& s) {
    Http::TestHeaderMapImpl headers{{"content-type", s}};
    return Common::hasGrpcContentType(headers);
  };
  EXPECT_FALSE(isGrpcContentType(""));
  EXPECT_FALSE(isGrpcContentType("application/text"));
  EXPECT_TRUE(isGrpcContentType("application/grpc"));
  EXPECT_TRUE(isGrpcContentType("application/grpc+"));
  EXPECT_TRUE(isGrpcContentType("application/grpc+foo"));
  EXPECT_FALSE(isGrpcContentType("application/grpc-"));
  EXPECT_FALSE(isGrpcContentType("application/grpc-web"));
  EXPECT_FALSE(isGrpcContentType("application/grpc-web+foo"));
}

TEST(GrpcCommonTest, IsGrpcResponseHeader) {
  Http::TestHeaderMapImpl grpc_status_only{{":status", "500"}, {"grpc-status", "14"}};
  EXPECT_TRUE(Common::isGrpcResponseHeader(grpc_status_only, true));
  EXPECT_FALSE(Common::isGrpcResponseHeader(grpc_status_only, false));

  Http::TestHeaderMapImpl grpc_response_header{{":status", "200"},
                                               {"content-type", "application/grpc"}};
  EXPECT_FALSE(Common::isGrpcResponseHeader(grpc_response_header, true));
  EXPECT_TRUE(Common::isGrpcResponseHeader(grpc_response_header, false));

  Http::TestHeaderMapImpl json_response_header{{":status", "200"},
                                               {"content-type", "application/json"}};
  EXPECT_FALSE(Common::isGrpcResponseHeader(json_response_header, true));
  EXPECT_FALSE(Common::isGrpcResponseHeader(json_response_header, false));
}

TEST(GrpcCommonTest, ValidateResponse) {
  {
    Http::ResponseMessageImpl response(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}});
    response.trailers(Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{"grpc-status", "0"}}});
    EXPECT_NO_THROW(Common::validateResponse(response));
  }
  {
    Http::ResponseMessageImpl response(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "503"}}});
    EXPECT_THROW_WITH_MESSAGE(Common::validateResponse(response), Exception,
                              "non-200 response code");
  }
  {
    Http::ResponseMessageImpl response(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}});
    response.trailers(Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{"grpc-status", "100"}}});
    EXPECT_THROW_WITH_MESSAGE(Common::validateResponse(response), Exception,
                              "bad grpc-status trailer");
  }
  {
    Http::ResponseMessageImpl response(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}});
    response.trailers(Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{"grpc-status", "4"}}});
    EXPECT_THROW_WITH_MESSAGE(Common::validateResponse(response), Exception, "");
  }
  {
    Http::ResponseMessageImpl response(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}});
    response.trailers(Http::HeaderMapPtr{
        new Http::TestHeaderMapImpl{{"grpc-status", "4"}, {"grpc-message", "custom error"}}});
    EXPECT_THROW_WITH_MESSAGE(Common::validateResponse(response), Exception, "custom error");
  }
  {
    Http::ResponseMessageImpl response(Http::HeaderMapPtr{
        new Http::TestHeaderMapImpl{{":status", "200"}, {"grpc-status", "100"}}});
    EXPECT_THROW_WITH_MESSAGE(Common::validateResponse(response), Exception,
                              "bad grpc-status header");
  }
  {
    Http::ResponseMessageImpl response(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}, {"grpc-status", "4"}}});
    EXPECT_THROW_WITH_MESSAGE(Common::validateResponse(response), Exception, "");
  }
  {
    Http::ResponseMessageImpl response(Http::HeaderMapPtr{new Http::TestHeaderMapImpl{
        {":status", "200"}, {"grpc-status", "4"}, {"grpc-message", "custom error"}}});
    EXPECT_THROW_WITH_MESSAGE(Common::validateResponse(response), Exception, "custom error");
  }
}

} // namespace Grpc
} // namespace Envoy
