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
  HeadersWrapper<Http::RequestHeaderMap> headers(nullptr);
  auto header = headers[CelValue::CreateStringView(Referer)];
  EXPECT_FALSE(header.has_value());
  EXPECT_EQ(0, headers.size());
  EXPECT_TRUE(headers.empty());
}

TEST(Context, RequestAttributes) {
  NiceMock<StreamInfo::MockStreamInfo> info;
  NiceMock<StreamInfo::MockStreamInfo> empty_info;
  Http::TestRequestHeaderMapImpl header_map{
      {":method", "POST"},           {":scheme", "http"},      {":path", "/meow?yes=1"},
      {":authority", "kittens.com"}, {"referer", "dogs.com"},  {"user-agent", "envoy-mobile"},
      {"content-length", "10"},      {"x-request-id", "blah"},
  };
  RequestWrapper request(&header_map, info);
  RequestWrapper empty_request(nullptr, empty_info);

  EXPECT_CALL(info, bytesReceived()).WillRepeatedly(Return(10));
  // "2018-04-03T23:06:09.123Z".
  const SystemTime start_time(std::chrono::milliseconds(1522796769123));
  EXPECT_CALL(info, startTime()).WillRepeatedly(Return(start_time));
  absl::optional<std::chrono::nanoseconds> dur = std::chrono::nanoseconds(15000000);
  EXPECT_CALL(info, requestComplete()).WillRepeatedly(Return(dur));
  EXPECT_CALL(info, protocol()).WillRepeatedly(Return(Http::Protocol::Http2));

  // stub methods
  EXPECT_EQ(0, request.size());
  EXPECT_FALSE(request.empty());

  {
    auto value = request[CelValue::CreateStringView(Undefined)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = request[CelValue::CreateInt64(13)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = request[CelValue::CreateStringView(Scheme)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("http", value.value().StringOrDie().value());
  }

  {
    auto value = empty_request[CelValue::CreateStringView(Scheme)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = request[CelValue::CreateStringView(Host)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("kittens.com", value.value().StringOrDie().value());
  }

  {
    auto value = request[CelValue::CreateStringView(Path)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("/meow?yes=1", value.value().StringOrDie().value());
  }

  {
    auto value = request[CelValue::CreateStringView(UrlPath)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("/meow", value.value().StringOrDie().value());
  }

  {
    auto value = request[CelValue::CreateStringView(Method)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("POST", value.value().StringOrDie().value());
  }

  {
    auto value = request[CelValue::CreateStringView(Referer)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("dogs.com", value.value().StringOrDie().value());
  }

  {
    auto value = request[CelValue::CreateStringView(UserAgent)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("envoy-mobile", value.value().StringOrDie().value());
  }

  {
    auto value = request[CelValue::CreateStringView(ID)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("blah", value.value().StringOrDie().value());
  }

  {
    auto value = request[CelValue::CreateStringView(Size)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(10, value.value().Int64OrDie());
  }

  {
    auto value = request[CelValue::CreateStringView(TotalSize)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    // this includes the headers size
    EXPECT_EQ(138, value.value().Int64OrDie());
  }

  {
    auto value = empty_request[CelValue::CreateStringView(TotalSize)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    // this includes the headers size
    EXPECT_EQ(0, value.value().Int64OrDie());
  }

  {
    auto value = request[CelValue::CreateStringView(Time)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsTimestamp());
    EXPECT_EQ("2018-04-03T23:06:09.123+00:00", absl::FormatTime(value.value().TimestampOrDie()));
  }

  {
    auto value = request[CelValue::CreateStringView(Headers)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsMap());
    auto& map = *value.value().MapOrDie();
    EXPECT_FALSE(map.empty());
    EXPECT_EQ(8, map.size());

    auto header = map[CelValue::CreateStringView(Referer)];
    EXPECT_TRUE(header.has_value());
    ASSERT_TRUE(header.value().IsString());
    EXPECT_EQ("dogs.com", header.value().StringOrDie().value());
  }

  {
    auto value = request[CelValue::CreateStringView(Duration)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsDuration());
    EXPECT_EQ("15ms", absl::FormatDuration(value.value().DurationOrDie()));
  }

  {
    auto value = empty_request[CelValue::CreateStringView(Duration)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = request[CelValue::CreateStringView(Protocol)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("HTTP/2", value.value().StringOrDie().value());
  }

  {
    auto value = empty_request[CelValue::CreateStringView(Protocol)];
    EXPECT_FALSE(value.has_value());
  }
}

TEST(Context, RequestFallbackAttributes) {
  NiceMock<StreamInfo::MockStreamInfo> info;
  Http::TestRequestHeaderMapImpl header_map{
      {":method", "POST"},
      {":scheme", "http"},
      {":path", "/meow?yes=1"},
  };
  RequestWrapper request(&header_map, info);

  EXPECT_CALL(info, bytesReceived()).WillRepeatedly(Return(10));

  {
    auto value = request[CelValue::CreateStringView(Size)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(10, value.value().Int64OrDie());
  }

  {
    auto value = request[CelValue::CreateStringView(UrlPath)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("/meow", value.value().StringOrDie().value());
  }
}

TEST(Context, ResponseAttributes) {
  NiceMock<StreamInfo::MockStreamInfo> info;
  NiceMock<StreamInfo::MockStreamInfo> empty_info;
  const std::string header_name = "test-header";
  const std::string trailer_name = "test-trailer";
  const std::string grpc_status = "grpc-status";
  Http::TestResponseHeaderMapImpl header_map{{header_name, "a"}};
  Http::TestResponseTrailerMapImpl trailer_map{{trailer_name, "b"}, {grpc_status, "8"}};
  ResponseWrapper response(&header_map, &trailer_map, info);
  ResponseWrapper empty_response(nullptr, nullptr, empty_info);

  EXPECT_CALL(info, responseCode()).WillRepeatedly(Return(404));
  EXPECT_CALL(info, bytesSent()).WillRepeatedly(Return(123));
  EXPECT_CALL(info, responseFlags()).WillRepeatedly(Return(0x1));

  {
    auto value = response[CelValue::CreateStringView(Undefined)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = response[CelValue::CreateInt64(13)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = response[CelValue::CreateStringView(Size)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(123, value.value().Int64OrDie());
  }

  {
    auto value = response[CelValue::CreateStringView(TotalSize)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(160, value.value().Int64OrDie());
  }

  {
    auto value = empty_response[CelValue::CreateStringView(TotalSize)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(0, value.value().Int64OrDie());
  }

  {
    auto value = response[CelValue::CreateStringView(Code)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(404, value.value().Int64OrDie());
  }

  {
    auto value = response[CelValue::CreateStringView(Headers)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsMap());
    auto& map = *value.value().MapOrDie();
    EXPECT_FALSE(map.empty());
    EXPECT_EQ(1, map.size());

    auto header = map[CelValue::CreateStringView(header_name)];
    EXPECT_TRUE(header.has_value());
    ASSERT_TRUE(header.value().IsString());
    EXPECT_EQ("a", header.value().StringOrDie().value());

    auto missing = map[CelValue::CreateStringView(Undefined)];
    EXPECT_FALSE(missing.has_value());
  }

  {
    auto value = response[CelValue::CreateStringView(Trailers)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsMap());
    auto& map = *value.value().MapOrDie();
    EXPECT_FALSE(map.empty());
    EXPECT_EQ(2, map.size());

    auto header = map[CelValue::CreateString(&trailer_name)];
    EXPECT_TRUE(header.has_value());
    ASSERT_TRUE(header.value().IsString());
    EXPECT_EQ("b", header.value().StringOrDie().value());
  }

  {
    auto value = response[CelValue::CreateStringView(Flags)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(0x1, value.value().Int64OrDie());
  }

  {
    auto value = response[CelValue::CreateStringView(GrpcStatus)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(0x8, value.value().Int64OrDie());
  }

  {
    auto value = empty_response[CelValue::CreateStringView(GrpcStatus)];
    EXPECT_FALSE(value.has_value());
  }

  {
    Http::TestResponseHeaderMapImpl header_map{{header_name, "a"}, {grpc_status, "7"}};
    Http::TestResponseTrailerMapImpl trailer_map{{trailer_name, "b"}};
    ResponseWrapper response_header_status(&header_map, &trailer_map, info);
    auto value = response_header_status[CelValue::CreateStringView(GrpcStatus)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(0x7, value.value().Int64OrDie());
  }
  {
    Http::TestResponseHeaderMapImpl header_map{{header_name, "a"}};
    Http::TestResponseTrailerMapImpl trailer_map{{trailer_name, "b"}};
    ResponseWrapper response_no_status(&header_map, &trailer_map, info);
    auto value = response_no_status[CelValue::CreateStringView(GrpcStatus)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(0xc, value.value().Int64OrDie()); // http:404 -> grpc:12
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> info_without_code;
    Http::TestResponseHeaderMapImpl header_map{{header_name, "a"}};
    Http::TestResponseTrailerMapImpl trailer_map{{trailer_name, "b"}};
    ResponseWrapper response_no_status(&header_map, &trailer_map, info_without_code);
    auto value = response_no_status[CelValue::CreateStringView(GrpcStatus)];
    EXPECT_FALSE(value.has_value());
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
    auto value = connection[CelValue::CreateStringView(Undefined)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = connection[CelValue::CreateInt64(13)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = source[CelValue::CreateStringView(Undefined)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = source[CelValue::CreateInt64(13)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = destination[CelValue::CreateStringView(Address)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("1.2.3.4:123", value.value().StringOrDie().value());
  }

  {
    auto value = destination[CelValue::CreateStringView(Port)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(123, value.value().Int64OrDie());
  }

  {
    auto value = source[CelValue::CreateStringView(Address)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("10.20.30.40:456", value.value().StringOrDie().value());
  }

  {
    auto value = source[CelValue::CreateStringView(Port)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(456, value.value().Int64OrDie());
  }

  {
    auto value = upstream[CelValue::CreateStringView(Address)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("10.1.2.3:679", value.value().StringOrDie().value());
  }

  {
    auto value = upstream[CelValue::CreateStringView(Port)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(679, value.value().Int64OrDie());
  }

  {
    auto value = connection[CelValue::CreateStringView(MTLS)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsBool());
    EXPECT_TRUE(value.value().BoolOrDie());
  }

  {
    auto value = connection[CelValue::CreateStringView(RequestedServerName)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(sni_name, value.value().StringOrDie().value());
  }

  {
    auto value = connection[CelValue::CreateStringView(TLSVersion)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(tls_version, value.value().StringOrDie().value());
  }

  {
    auto value = connection[CelValue::CreateStringView(DNSSanLocalCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(dns_sans_local[0], value.value().StringOrDie().value());
  }

  {
    auto value = connection[CelValue::CreateStringView(DNSSanPeerCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(dns_sans_peer[0], value.value().StringOrDie().value());
  }

  {
    auto value = connection[CelValue::CreateStringView(URISanLocalCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(uri_sans_local[0], value.value().StringOrDie().value());
  }

  {
    auto value = connection[CelValue::CreateStringView(URISanPeerCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(uri_sans_peer[0], value.value().StringOrDie().value());
  }

  {
    auto value = connection[CelValue::CreateStringView(SubjectLocalCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(subject_local, value.value().StringOrDie().value());
  }

  {
    auto value = connection[CelValue::CreateStringView(SubjectPeerCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(subject_peer, value.value().StringOrDie().value());
  }

  {
    auto value = upstream[CelValue::CreateStringView(TLSVersion)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(tls_version, value.value().StringOrDie().value());
  }

  {
    auto value = upstream[CelValue::CreateStringView(DNSSanLocalCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(dns_sans_local[0], value.value().StringOrDie().value());
  }

  {
    auto value = upstream[CelValue::CreateStringView(DNSSanPeerCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(dns_sans_peer[0], value.value().StringOrDie().value());
  }

  {
    auto value = upstream[CelValue::CreateStringView(URISanLocalCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(uri_sans_local[0], value.value().StringOrDie().value());
  }

  {
    auto value = upstream[CelValue::CreateStringView(URISanPeerCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(uri_sans_peer[0], value.value().StringOrDie().value());
  }

  {
    auto value = upstream[CelValue::CreateStringView(SubjectLocalCertificate)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(subject_local, value.value().StringOrDie().value());
  }

  {
    auto value = upstream[CelValue::CreateStringView(SubjectPeerCertificate)];
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
