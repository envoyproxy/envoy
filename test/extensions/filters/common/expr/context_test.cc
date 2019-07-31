#include "common/network/utility.h"

#include "extensions/filters/common/expr/context.h"

#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Const;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace {

constexpr absl::string_view Undefined = "undefined";

TEST(Context, RequestAttributes) {
  NiceMock<StreamInfo::MockStreamInfo> info;
  Http::TestHeaderMapImpl header_map{
      {":method", "POST"},           {":scheme", "http"},      {":path", "/meow"},
      {":authority", "kittens.com"}, {"referer", "dogs.com"},  {"user-agent", "envoy-mobile"},
      {"content-length", "10"},      {"x-request-id", "blah"},
  };
  RequestWrapper request(header_map, info);

  EXPECT_CALL(info, bytesReceived()).WillRepeatedly(Return(10));
  // "2018-04-03T23:06:09.123Z".
  const SystemTime start_time(std::chrono::milliseconds(1522796769123));
  EXPECT_CALL(info, startTime()).WillRepeatedly(Return(start_time));
  absl::optional<std::chrono::nanoseconds> dur = std::chrono::nanoseconds(15000000);
  EXPECT_CALL(info, requestComplete()).WillRepeatedly(Return(dur));

  {
    auto value = request[CelValue::CreateString(Undefined)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = request[CelValue::CreateString(Scheme)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("http", value.value().StringOrDie().value());
  }
  {
    auto value = request[CelValue::CreateString(Host)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("kittens.com", value.value().StringOrDie().value());
  }

  {
    auto value = request[CelValue::CreateString(Path)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("/meow", value.value().StringOrDie().value());
  }

  {
    auto value = request[CelValue::CreateString(Method)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("POST", value.value().StringOrDie().value());
  }

  {
    auto value = request[CelValue::CreateString(Referer)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("dogs.com", value.value().StringOrDie().value());
  }

  {
    auto value = request[CelValue::CreateString(UserAgent)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("envoy-mobile", value.value().StringOrDie().value());
  }

  {
    auto value = request[CelValue::CreateString(ID)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("blah", value.value().StringOrDie().value());
  }

  {
    auto value = request[CelValue::CreateString(Size)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(10, value.value().Int64OrDie());
  }

  {
    auto value = request[CelValue::CreateString(TotalSize)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    // this includes the headers size
    EXPECT_EQ(132, value.value().Int64OrDie());
  }

  {
    auto value = request[CelValue::CreateString(Time)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsTimestamp());
    EXPECT_EQ("2018-04-03T23:06:09.123+00:00", absl::FormatTime(value.value().TimestampOrDie()));
  }

  {
    auto value = request[CelValue::CreateString(Headers)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsMap());
    auto& map = *value.value().MapOrDie();
    EXPECT_FALSE(map.empty());
    EXPECT_EQ(8, map.size());

    auto header = map[CelValue::CreateString(Referer)];
    EXPECT_TRUE(header.has_value());
    ASSERT_TRUE(header.value().IsString());
    EXPECT_EQ("dogs.com", header.value().StringOrDie().value());
  }

  {
    auto value = request[CelValue::CreateString(Duration)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsDuration());
    EXPECT_EQ("15ms", absl::FormatDuration(value.value().DurationOrDie()));
  }
}

TEST(Context, ResponseAttributes) {
  NiceMock<StreamInfo::MockStreamInfo> info;
  ResponseWrapper response(info);

  EXPECT_CALL(info, responseCode()).WillRepeatedly(Return(404));
  EXPECT_CALL(info, bytesSent()).WillRepeatedly(Return(123));

  {
    auto value = response[CelValue::CreateString(Undefined)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = response[CelValue::CreateString(Size)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(123, value.value().Int64OrDie());
  }

  {
    auto value = response[CelValue::CreateString(Code)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(404, value.value().Int64OrDie());
  }
}

TEST(Context, ConnectionAttributes) {
  NiceMock<StreamInfo::MockStreamInfo> info;
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  NiceMock<Ssl::MockConnectionInfo> connection_info;
  ConnectionWrapper connection(info);

  Network::Address::InstanceConstSharedPtr local =
      Network::Utility::parseInternetAddress("1.2.3.4", 123, false);
  Network::Address::InstanceConstSharedPtr remote =
      Network::Utility::parseInternetAddress("10.20.30.40", 456, false);
  Network::Address::InstanceConstSharedPtr upstream =
      Network::Utility::parseInternetAddress("10.1.2.3", 679, false);
  EXPECT_CALL(info, downstreamLocalAddress()).WillRepeatedly(ReturnRef(local));
  EXPECT_CALL(info, downstreamRemoteAddress()).WillRepeatedly(ReturnRef(remote));
  EXPECT_CALL(info, downstreamSslConnection()).WillRepeatedly(Return(&connection_info));
  EXPECT_CALL(info, upstreamHost()).WillRepeatedly(Return(host));
  EXPECT_CALL(connection_info, peerCertificatePresented()).WillRepeatedly(Return(true));
  EXPECT_CALL(*host, address()).WillRepeatedly(Return(upstream));

  {
    auto value = connection[CelValue::CreateString(Undefined)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = connection[CelValue::CreateString(LocalAddress)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("1.2.3.4:123", value.value().StringOrDie().value());
  }

  {
    auto value = connection[CelValue::CreateString(LocalPort)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(123, value.value().Int64OrDie());
  }

  {
    auto value = connection[CelValue::CreateString(RemoteAddress)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("10.20.30.40:456", value.value().StringOrDie().value());
  }

  {
    auto value = connection[CelValue::CreateString(RemotePort)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(456, value.value().Int64OrDie());
  }

  {
    auto value = connection[CelValue::CreateString(UpstreamAddress)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("10.1.2.3:679", value.value().StringOrDie().value());
  }

  {
    auto value = connection[CelValue::CreateString(UpstreamPort)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(679, value.value().Int64OrDie());
  }

  {
    auto value = connection[CelValue::CreateString(MTLS)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsBool());
    EXPECT_TRUE(value.value().BoolOrDie());
  }
}

} // namespace
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
