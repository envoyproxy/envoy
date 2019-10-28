#include "common/network/utility.h"

#include "extensions/filters/common/expr/context.h"

#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace {

constexpr absl::string_view Undefined = "undefined";

TEST(Context, EmptyHeadersAttributes) {
  HeadersWrapper headers(nullptr);
  auto header = headers[CelValue::CreateString(Referer)];
  EXPECT_FALSE(header.has_value());
  EXPECT_EQ(0, headers.size());
  EXPECT_TRUE(headers.empty());
}

TEST(Context, RequestAttributes) {
  NiceMock<StreamInfo::MockStreamInfo> info;
  Http::TestHeaderMapImpl header_map{
      {":method", "POST"},           {":scheme", "http"},      {":path", "/meow?yes=1"},
      {":authority", "kittens.com"}, {"referer", "dogs.com"},  {"user-agent", "envoy-mobile"},
      {"content-length", "10"},      {"x-request-id", "blah"},
  };
  RequestWrapper request(&header_map, info);

  EXPECT_CALL(info, bytesReceived()).WillRepeatedly(Return(10));
  // "2018-04-03T23:06:09.123Z".
  const SystemTime start_time(std::chrono::milliseconds(1522796769123));
  EXPECT_CALL(info, startTime()).WillRepeatedly(Return(start_time));
  absl::optional<std::chrono::nanoseconds> dur = std::chrono::nanoseconds(15000000);
  EXPECT_CALL(info, requestComplete()).WillRepeatedly(Return(dur));

  // stub methods
  EXPECT_EQ(0, request.size());
  EXPECT_FALSE(request.empty());

  {
    auto value = request[CelValue::CreateString(Undefined)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = request[CelValue::CreateInt64(13)];
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
    EXPECT_EQ("/meow?yes=1", value.value().StringOrDie().value());
  }

  {
    auto value = request[CelValue::CreateString(UrlPath)];
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
    EXPECT_EQ(138, value.value().Int64OrDie());
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

TEST(Context, RequestFallbackAttributes) {
  NiceMock<StreamInfo::MockStreamInfo> info;
  Http::TestHeaderMapImpl header_map{
      {":method", "POST"},
      {":scheme", "http"},
      {":path", "/meow?yes=1"},
  };
  RequestWrapper request(&header_map, info);

  EXPECT_CALL(info, bytesReceived()).WillRepeatedly(Return(10));

  {
    auto value = request[CelValue::CreateString(Size)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(10, value.value().Int64OrDie());
  }

  {
    auto value = request[CelValue::CreateString(UrlPath)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("/meow", value.value().StringOrDie().value());
  }
}

TEST(Context, ResponseAttributes) {
  NiceMock<StreamInfo::MockStreamInfo> info;
  const std::string header_name = "test-header";
  const std::string trailer_name = "test-trailer";
  Http::TestHeaderMapImpl header_map{{header_name, "a"}};
  Http::TestHeaderMapImpl trailer_map{{trailer_name, "b"}};
  ResponseWrapper response(&header_map, &trailer_map, info);

  EXPECT_CALL(info, responseCode()).WillRepeatedly(Return(404));
  EXPECT_CALL(info, bytesSent()).WillRepeatedly(Return(123));

  {
    auto value = response[CelValue::CreateString(Undefined)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = response[CelValue::CreateInt64(13)];
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

  {
    auto value = response[CelValue::CreateString(Headers)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsMap());
    auto& map = *value.value().MapOrDie();
    EXPECT_FALSE(map.empty());
    EXPECT_EQ(1, map.size());

    auto header = map[CelValue::CreateString(header_name)];
    EXPECT_TRUE(header.has_value());
    ASSERT_TRUE(header.value().IsString());
    EXPECT_EQ("a", header.value().StringOrDie().value());

    auto missing = map[CelValue::CreateString(Undefined)];
    EXPECT_FALSE(missing.has_value());
  }

  {
    auto value = response[CelValue::CreateString(Trailers)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsMap());
    auto& map = *value.value().MapOrDie();
    EXPECT_FALSE(map.empty());
    EXPECT_EQ(1, map.size());

    auto header = map[CelValue::CreateString(trailer_name)];
    EXPECT_TRUE(header.has_value());
    ASSERT_TRUE(header.value().IsString());
    EXPECT_EQ("b", header.value().StringOrDie().value());
  }
}

TEST(Context, ConnectionAttributes) {
  NiceMock<StreamInfo::MockStreamInfo> info;
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> upstream_host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  auto downstream_ssl_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  auto upstream_ssl_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ConnectionWrapper connection(info);
  UpstreamWrapper upstream(info);
  PeerWrapper source(info, false);
  PeerWrapper destination(info, true);

  Network::Address::InstanceConstSharedPtr local =
      Network::Utility::parseInternetAddress("1.2.3.4", 123, false);
  Network::Address::InstanceConstSharedPtr remote =
      Network::Utility::parseInternetAddress("10.20.30.40", 456, false);
  Network::Address::InstanceConstSharedPtr upstream_address =
      Network::Utility::parseInternetAddress("10.1.2.3", 679, false);
  const std::string sni_name = "kittens.com";
  EXPECT_CALL(info, downstreamLocalAddress()).WillRepeatedly(ReturnRef(local));
  EXPECT_CALL(info, downstreamRemoteAddress()).WillRepeatedly(ReturnRef(remote));
  EXPECT_CALL(info, downstreamSslConnection()).WillRepeatedly(Return(downstream_ssl_info));
  EXPECT_CALL(info, upstreamSslConnection()).WillRepeatedly(Return(upstream_ssl_info));
  EXPECT_CALL(info, upstreamHost()).WillRepeatedly(Return(upstream_host));
  EXPECT_CALL(info, requestedServerName()).WillRepeatedly(ReturnRef(sni_name));
  EXPECT_CALL(*downstream_ssl_info, peerCertificatePresented()).WillRepeatedly(Return(true));
  EXPECT_CALL(*upstream_host, address()).WillRepeatedly(Return(upstream_address));

  const std::string tls_version = "TLSv1";
  EXPECT_CALL(*downstream_ssl_info, tlsVersion()).WillRepeatedly(ReturnRef(tls_version));
  EXPECT_CALL(*upstream_ssl_info, tlsVersion()).WillRepeatedly(ReturnRef(tls_version));
  std::vector<std::string> dns_sans_peer = {"www.peer.com"};
  EXPECT_CALL(*downstream_ssl_info, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans_peer));
  EXPECT_CALL(*upstream_ssl_info, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans_peer));
  std::vector<std::string> dns_sans_local = {"www.local.com"};
  EXPECT_CALL(*downstream_ssl_info, dnsSansLocalCertificate())
      .WillRepeatedly(Return(dns_sans_local));
  EXPECT_CALL(*upstream_ssl_info, dnsSansLocalCertificate()).WillRepeatedly(Return(dns_sans_local));
  std::vector<std::string> uri_sans_peer = {"www.peer.com/uri"};
  EXPECT_CALL(*downstream_ssl_info, uriSanPeerCertificate()).WillRepeatedly(Return(uri_sans_peer));
  EXPECT_CALL(*upstream_ssl_info, uriSanPeerCertificate()).WillRepeatedly(Return(uri_sans_peer));
  std::vector<std::string> uri_sans_local = {"www.local.com/uri"};
  EXPECT_CALL(*downstream_ssl_info, uriSanLocalCertificate())
      .WillRepeatedly(Return(uri_sans_local));
  EXPECT_CALL(*upstream_ssl_info, uriSanLocalCertificate()).WillRepeatedly(Return(uri_sans_local));
  const std::string subject_local = "local.com";
  EXPECT_CALL(*downstream_ssl_info, subjectLocalCertificate())
      .WillRepeatedly(ReturnRef(subject_local));
  EXPECT_CALL(*upstream_ssl_info, subjectLocalCertificate())
      .WillRepeatedly(ReturnRef(subject_local));
  const std::string subject_peer = "peer.com";
  EXPECT_CALL(*downstream_ssl_info, subjectPeerCertificate())
      .WillRepeatedly(ReturnRef(subject_peer));
  EXPECT_CALL(*upstream_ssl_info, subjectPeerCertificate()).WillRepeatedly(ReturnRef(subject_peer));

  {
    auto value = connection[CelValue::CreateString(Undefined)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = connection[CelValue::CreateInt64(13)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = source[CelValue::CreateString(Undefined)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = source[CelValue::CreateInt64(13)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = destination[CelValue::CreateString(Address)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("1.2.3.4:123", value.value().StringOrDie().value());
  }

  {
    auto value = destination[CelValue::CreateString(Port)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(123, value.value().Int64OrDie());
  }

  {
    auto value = source[CelValue::CreateString(Address)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("10.20.30.40:456", value.value().StringOrDie().value());
  }

  {
    auto value = source[CelValue::CreateString(Port)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(456, value.value().Int64OrDie());
  }

  {
    auto value = upstream[CelValue::CreateString(Address)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("10.1.2.3:679", value.value().StringOrDie().value());
  }

  {
    auto value = upstream[CelValue::CreateString(Port)];
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

  {
    auto value = connection[CelValue::CreateString(RequestedServerName)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(sni_name, value.value().StringOrDie().value());
  }

  {
    auto value = connection[CelValue::CreateString(TLSVersion)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(tls_version, value.value().StringOrDie().value());
  }

  {
    auto value = connection[CelValue::CreateString(DNSSanLocalCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(dns_sans_local[0], value.value().StringOrDie().value());
  }

  {
    auto value = connection[CelValue::CreateString(DNSSanPeerCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(dns_sans_peer[0], value.value().StringOrDie().value());
  }

  {
    auto value = connection[CelValue::CreateString(URISanLocalCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(uri_sans_local[0], value.value().StringOrDie().value());
  }

  {
    auto value = connection[CelValue::CreateString(URISanPeerCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(uri_sans_peer[0], value.value().StringOrDie().value());
  }

  {
    auto value = connection[CelValue::CreateString(SubjectLocalCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(subject_local, value.value().StringOrDie().value());
  }

  {
    auto value = connection[CelValue::CreateString(SubjectPeerCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(subject_peer, value.value().StringOrDie().value());
  }

  {
    auto value = upstream[CelValue::CreateString(TLSVersion)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(tls_version, value.value().StringOrDie().value());
  }

  {
    auto value = upstream[CelValue::CreateString(DNSSanLocalCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(dns_sans_local[0], value.value().StringOrDie().value());
  }

  {
    auto value = upstream[CelValue::CreateString(DNSSanPeerCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(dns_sans_peer[0], value.value().StringOrDie().value());
  }

  {
    auto value = upstream[CelValue::CreateString(URISanLocalCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(uri_sans_local[0], value.value().StringOrDie().value());
  }

  {
    auto value = upstream[CelValue::CreateString(URISanPeerCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(uri_sans_peer[0], value.value().StringOrDie().value());
  }

  {
    auto value = upstream[CelValue::CreateString(SubjectLocalCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(subject_local, value.value().StringOrDie().value());
  }

  {
    auto value = upstream[CelValue::CreateString(SubjectPeerCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(subject_peer, value.value().StringOrDie().value());
  }
}

} // namespace
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
