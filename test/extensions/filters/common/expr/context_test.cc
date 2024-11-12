#include "source/common/network/address_impl.h"
#include "source/common/network/filter_state_dst_address.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/filters/common/expr/cel_state.h"
#include "source/extensions/filters/common/expr/context.h"

#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

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
  Protobuf::Arena arena;
  HeadersWrapper<Http::RequestHeaderMap> headers(arena, nullptr);
  auto header = headers[CelValue::CreateStringView(Referer)];
  EXPECT_FALSE(header.has_value());
  EXPECT_EQ(0, headers.size());
  EXPECT_TRUE(headers.empty());
  EXPECT_TRUE(headers.ListKeys().ok());
  EXPECT_EQ(0, headers.ListKeys().value()->size());
}

TEST(Context, InvalidRequest) {
  Http::TestRequestHeaderMapImpl header_map{{"referer", "dogs.com"}};
  Protobuf::Arena arena;
  HeadersWrapper<Http::RequestHeaderMap> headers(arena, &header_map);
  auto header = headers[CelValue::CreateStringView("referer\n")];
  EXPECT_FALSE(header.has_value());
}

TEST(Context, InvalidRequestLegacy) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.consistent_header_validation", "false"}});

  Http::TestRequestHeaderMapImpl header_map{{"referer", "dogs.com"}};
  Protobuf::Arena arena;
  HeadersWrapper<Http::RequestHeaderMap> headers(arena, &header_map);
  auto header = headers[CelValue::CreateStringView("referer\n")];
  EXPECT_FALSE(header.has_value());
}

TEST(Context, RequestAttributes) {
  NiceMock<StreamInfo::MockStreamInfo> info;
  NiceMock<StreamInfo::MockStreamInfo> empty_info;
  Http::TestRequestHeaderMapImpl header_map{
      {":method", "POST"},           {":scheme", "http"},      {":path", "/meow?yes=1"},
      {":authority", "kittens.com"}, {"referer", "dogs.com"},  {"user-agent", "envoy-mobile"},
      {"content-length", "10"},      {"x-request-id", "blah"}, {"double-header", "foo"},
      {"double-header", "bar"}};
  Protobuf::Arena arena;
  RequestWrapper request(arena, &header_map, info);
  RequestWrapper empty_request(arena, nullptr, empty_info);

  EXPECT_CALL(info, bytesReceived()).WillRepeatedly(Return(10));
  // "2018-04-03T23:06:09.123Z".
  const SystemTime start_time(std::chrono::milliseconds(1522796769123));
  EXPECT_CALL(info, startTime()).WillRepeatedly(Return(start_time));
  absl::optional<std::chrono::nanoseconds> dur = std::chrono::nanoseconds(15000000);
  EXPECT_CALL(info, requestComplete()).WillRepeatedly(Return(dur));
  EXPECT_CALL(info, protocol()).WillRepeatedly(Return(Http::Protocol::Http2));

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
    auto value = request[CelValue::CreateStringView(Query)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("yes=1", value.value().StringOrDie().value());
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
    EXPECT_EQ(170, value.value().Int64OrDie());
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
    EXPECT_EQ(9, map.size());
    EXPECT_TRUE(map.ListKeys().ok());
    EXPECT_EQ(9, map.ListKeys().value()->size());

    auto header = map[CelValue::CreateStringView(Referer)];
    EXPECT_TRUE(header.has_value());
    ASSERT_TRUE(header.value().IsString());
    EXPECT_EQ("dogs.com", header.value().StringOrDie().value());

    auto header2 = map[CelValue::CreateStringView("double-header")];
    EXPECT_TRUE(header2.has_value());
    ASSERT_TRUE(header2.value().IsString());
    EXPECT_EQ("foo,bar", header2.value().StringOrDie().value());
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
      {":path", "/meow"},
  };
  Protobuf::Arena arena;
  RequestWrapper request(arena, &header_map, info);

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

  {
    auto value = request[CelValue::CreateStringView(Query)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("", value.value().StringOrDie().value());
  }
}

TEST(Context, RequestPathFragment) {
  NiceMock<StreamInfo::MockStreamInfo> info;
  Http::TestRequestHeaderMapImpl header_map{
      {":method", "POST"},
      {":scheme", "http"},
      {":path", "/meow?page=1&item=3#heading"},
  };
  Protobuf::Arena arena;
  RequestWrapper request(arena, &header_map, info);
  {
    auto value = request[CelValue::CreateStringView(Query)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("page=1&item=3", value.value().StringOrDie().value());
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
  Protobuf::Arena arena;
  ResponseWrapper response(arena, &header_map, &trailer_map, info);
  ResponseWrapper empty_response(arena, nullptr, nullptr, empty_info);

  EXPECT_CALL(info, responseCode()).WillRepeatedly(Return(404));
  EXPECT_CALL(info, bytesSent()).WillRepeatedly(Return(123));
  EXPECT_CALL(info, legacyResponseFlags()).WillRepeatedly(Return(0x1));

  const absl::optional<std::string> code_details = "unauthorized";
  EXPECT_CALL(info, responseCodeDetails()).WillRepeatedly(ReturnRef(code_details));

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
    auto value = response[CelValue::CreateStringView(CodeDetails)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(code_details.value(), value.value().StringOrDie().value());
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
    auto value = empty_response[CelValue::CreateStringView(Code)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = empty_response[CelValue::CreateStringView(CodeDetails)];
    EXPECT_FALSE(value.has_value());
  }

  {
    info.setUpstreamInfo(std::make_shared<StreamInfo::UpstreamInfoImpl>());
    StreamInfo::UpstreamTiming& upstream_timing = info.upstreamInfo()->upstreamTiming();
    upstream_timing.onFirstUpstreamTxByteSent(info.timeSource());
    upstream_timing.onLastUpstreamRxByteReceived(info.timeSource());
    EXPECT_TRUE(response[CelValue::CreateStringView(BackendLatency)].has_value());
  }

  {
    Http::TestResponseHeaderMapImpl header_map{{header_name, "a"}, {grpc_status, "7"}};
    Http::TestResponseTrailerMapImpl trailer_map{{trailer_name, "b"}};
    Protobuf::Arena arena;
    ResponseWrapper response_header_status(arena, &header_map, &trailer_map, info);
    auto value = response_header_status[CelValue::CreateStringView(GrpcStatus)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(0x7, value.value().Int64OrDie());
  }
  {
    Http::TestResponseHeaderMapImpl header_map{{header_name, "a"}};
    Http::TestResponseTrailerMapImpl trailer_map{{trailer_name, "b"}};
    Protobuf::Arena arena;
    ResponseWrapper response_no_status(arena, &header_map, &trailer_map, info);
    auto value = response_no_status[CelValue::CreateStringView(GrpcStatus)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(0xc, value.value().Int64OrDie()); // http:404 -> grpc:12
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> info_without_code;
    Http::TestResponseHeaderMapImpl header_map{{header_name, "a"}};
    Http::TestResponseTrailerMapImpl trailer_map{{trailer_name, "b"}};
    Protobuf::Arena arena;
    ResponseWrapper response_no_status(arena, &header_map, &trailer_map, info_without_code);
    auto value = response_no_status[CelValue::CreateStringView(GrpcStatus)];
    EXPECT_FALSE(value.has_value());
  }
}

TEST(Context, ConnectionFallbackAttributes) {
  NiceMock<StreamInfo::MockStreamInfo> info;
  Protobuf::Arena arena;
  ConnectionWrapper connection(arena, info);
  UpstreamWrapper upstream(arena, info);
  {
    auto value = connection[CelValue::CreateStringView(Undefined)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = connection[CelValue::CreateStringView(ID)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = upstream[CelValue::CreateStringView(Undefined)];
    EXPECT_FALSE(value.has_value());
  }

  {
    auto value = upstream[CelValue::CreateInt64(1)];
    EXPECT_FALSE(value.has_value());
  }
}

TEST(Context, ConnectionAttributes) {
  NiceMock<StreamInfo::MockStreamInfo> info;
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> upstream_host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  auto downstream_ssl_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  auto upstream_ssl_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  Protobuf::Arena arena;
  ConnectionWrapper connection(arena, info);
  UpstreamWrapper upstream(arena, info);
  PeerWrapper source(arena, info, false);
  PeerWrapper destination(arena, info, true);

  Network::Address::InstanceConstSharedPtr local =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 123, false);
  Network::Address::InstanceConstSharedPtr remote =
      Network::Utility::parseInternetAddressNoThrow("10.20.30.40", 456, false);
  Network::Address::InstanceConstSharedPtr upstream_address =
      Network::Utility::parseInternetAddressNoThrow("10.1.2.3", 679, false);
  Network::Address::InstanceConstSharedPtr upstream_local_address =
      Network::Utility::parseInternetAddressNoThrow("10.1.2.3", 1000, false);
  const std::string sni_name = "kittens.com";
  info.downstream_connection_info_provider_->setLocalAddress(local);
  info.downstream_connection_info_provider_->setRemoteAddress(remote);
  info.downstream_connection_info_provider_->setRequestedServerName(sni_name);
  info.downstream_connection_info_provider_->setSslConnection(downstream_ssl_info);
  const std::string upstream_transport_failure_reason = "ConnectionTermination";
  info.upstreamInfo()->setUpstreamSslConnection(upstream_ssl_info);
  info.upstreamInfo()->setUpstreamHost(upstream_host);
  info.upstreamInfo()->setUpstreamLocalAddress(upstream_local_address);
  info.upstreamInfo()->setUpstreamTransportFailureReason(upstream_transport_failure_reason);
  EXPECT_CALL(info, connectionID()).WillRepeatedly(Return(123));
  info.downstream_connection_info_provider_->setConnectionID(123);
  const absl::optional<std::string> connection_termination_details = "unauthorized";
  EXPECT_CALL(info, connectionTerminationDetails())
      .WillRepeatedly(ReturnRef(connection_termination_details));
  const std::string downstream_transport_failure_reason = "TlsError";
  info.setDownstreamTransportFailureReason(downstream_transport_failure_reason);

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
  const std::string peer_certificate_digest = "c58ccaf8e9276ebd095652e56e89c7d56e92e6c0";
  EXPECT_CALL(*downstream_ssl_info, sha256PeerCertificateDigest())
      .WillRepeatedly(ReturnRef(peer_certificate_digest));
  EXPECT_CALL(*upstream_ssl_info, sha256PeerCertificateDigest())
      .WillRepeatedly(ReturnRef(peer_certificate_digest));

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
    auto value = connection[CelValue::CreateStringView(SHA256PeerCertificateDigest)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(peer_certificate_digest, value.value().StringOrDie().value());
  }

  {
    auto value = connection[CelValue::CreateStringView(ID)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsUint64());
    EXPECT_EQ(123, value.value().Uint64OrDie());
  }

  {
    auto value = connection[CelValue::CreateStringView(ConnectionTerminationDetails)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(connection_termination_details.value(), value.value().StringOrDie().value());
  }

  {
    auto value = connection[CelValue::CreateStringView(DownstreamTransportFailureReason)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(downstream_transport_failure_reason, value.value().StringOrDie().value());
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

  {
    auto value = upstream[CelValue::CreateStringView(SHA256PeerCertificateDigest)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(peer_certificate_digest, value.value().StringOrDie().value());
  }

  {
    auto value = upstream[CelValue::CreateStringView(UpstreamLocalAddress)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(upstream_local_address->asStringView(), value.value().StringOrDie().value());
  }

  {
    auto value = upstream[CelValue::CreateStringView(UpstreamTransportFailureReason)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(upstream_transport_failure_reason, value.value().StringOrDie().value());
  }
}

TEST(Context, FilterStateAttributes) {
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::FilterChain);
  ProtobufWkt::Arena arena;
  FilterStateWrapper wrapper(arena, filter_state);
  auto status_or = wrapper.ListKeys(&arena);
  EXPECT_EQ(status_or.status().message(), "ListKeys() is not implemented");

  const std::string key = "filter_state_key";
  const std::string serialized = "filter_state_value";
  const std::string missing = "missing_key";

  auto accessor = std::make_shared<Envoy::Router::StringAccessorImpl>(serialized);
  filter_state.setData(key, accessor, StreamInfo::FilterState::StateType::ReadOnly);

  EXPECT_EQ(0, wrapper.size());

  {
    auto value = wrapper[CelValue::CreateStringView(missing)];
    EXPECT_FALSE(value.has_value());
  }
  {
    auto value = wrapper[CelValue::CreateInt64(1)];
    EXPECT_FALSE(value.has_value());
  }
  {
    auto value = wrapper[CelValue::CreateStringView(key)];
    EXPECT_TRUE(value.has_value());
    EXPECT_TRUE(value.value().IsBytes());
    EXPECT_EQ(serialized, value.value().BytesOrDie().value());
  }

  CelStatePrototype prototype(true, CelStateType::Protobuf,
                              "type.googleapis.com/google.protobuf.DoubleValue",
                              StreamInfo::FilterState::LifeSpan::FilterChain);
  auto cel_state = std::make_shared<CelState>(prototype);
  ProtobufWkt::DoubleValue v;
  v.set_value(1.0);
  cel_state->setValue(v.SerializeAsString());
  EXPECT_TRUE(cel_state->serializeAsString().has_value());
  const std::string cel_key = "cel_state_key";
  filter_state.setData(cel_key, cel_state, StreamInfo::FilterState::StateType::ReadOnly);

  {
    auto value = wrapper[CelValue::CreateStringView(cel_key)];
    EXPECT_TRUE(value.has_value());
    EXPECT_TRUE(value.value().IsDouble());
    EXPECT_EQ(value.value().DoubleOrDie(), 1.0);
  }

  const std::string address_key = "envoy.network.transport_socket.original_dst_address";
  const std::string ip_string = "ip";
  const std::string port_string = "port";
  filter_state.setData(address_key,
                       std::make_unique<Network::AddressObject>(
                           std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11", 6666)),
                       StreamInfo::FilterState::StateType::ReadOnly);
  {
    auto value = wrapper[CelValue::CreateStringView(address_key)];
    ASSERT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsMap());
    auto& map = *value.value().MapOrDie();
    EXPECT_FALSE(map[CelValue::CreateInt64(5)].has_value());
    EXPECT_EQ(0, map.size());
    EXPECT_TRUE(map.empty());
    EXPECT_EQ(0, map.ListKeys().value()->size());
    auto ip = map[CelValue::CreateStringView(ip_string)];
    EXPECT_TRUE(ip.has_value());
    EXPECT_TRUE(ip->IsString());
    EXPECT_EQ("10.10.11.11", ip->StringOrDie().value());
    auto port = map[CelValue::CreateStringView(port_string)];
    EXPECT_TRUE(port.has_value());
    EXPECT_TRUE(port->IsInt64());
    EXPECT_EQ(6666, port->Int64OrDie());
    auto other = map[CelValue::CreateStringView(address_key)];
    EXPECT_FALSE(other.has_value());
  }
}

TEST(Context, XDSAttributes) {
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<StreamInfo::MockStreamInfo> info;
  std::shared_ptr<NiceMock<Upstream::MockClusterInfo>> cluster_info(
      new NiceMock<Upstream::MockClusterInfo>());
  EXPECT_CALL(info, upstreamClusterInfo()).WillRepeatedly(Return(cluster_info));
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> upstream_host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  auto host_metadata = std::make_shared<const envoy::config::core::v3::Metadata>();
  EXPECT_CALL(*upstream_host, metadata()).WillRepeatedly(Return(host_metadata));
  info.upstreamInfo()->setUpstreamHost(upstream_host);
  std::shared_ptr<NiceMock<Router::MockRoute>> route{new NiceMock<Router::MockRoute>()};
  EXPECT_CALL(info, route()).WillRepeatedly(Return(route));
  const std::string chain_name = "fake_filter_chain_name";

  auto filter_chain_info = std::make_shared<NiceMock<Network::MockFilterChainInfo>>();
  filter_chain_info->filter_chain_name_ = "fake_filter_chain_name";
  info.downstream_connection_info_provider_->setFilterChainInfo(filter_chain_info);

  auto listener_info = std::make_shared<NiceMock<Network::MockListenerInfo>>();
  envoy::config::core::v3::Metadata listener_metadata;
  EXPECT_CALL(*listener_info, metadata()).WillRepeatedly(ReturnRef(listener_metadata));
  EXPECT_CALL(*listener_info, direction())
      .WillRepeatedly(Return(envoy::config::core::v3::TrafficDirection::OUTBOUND));
  info.downstream_connection_info_provider_->setListenerInfo(listener_info);

  Protobuf::Arena arena;
  XDSWrapper wrapper(arena, &info, &local_info);

  {
    const auto value = wrapper[CelValue::CreateStringView(ClusterName)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("fake_cluster", value.value().StringOrDie().value());
  }
  {
    const auto value = wrapper[CelValue::CreateStringView(ClusterMetadata)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsMessage());
    EXPECT_EQ(&cluster_info->metadata_, value.value().MessageOrDie());
  }
  {
    const auto value = wrapper[CelValue::CreateStringView(RouteName)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ("fake_route_name", value.value().StringOrDie().value());
  }
  {
    const auto value = wrapper[CelValue::CreateStringView(RouteMetadata)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsMessage());
    EXPECT_EQ(&route->metadata_, value.value().MessageOrDie());
  }
  {
    const auto value = wrapper[CelValue::CreateStringView(UpstreamHostMetadata)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsMessage());
    EXPECT_EQ(host_metadata.get(), value.value().MessageOrDie());
  }
  {
    const auto value = wrapper[CelValue::CreateStringView(FilterChainName)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsString());
    EXPECT_EQ(chain_name, value.value().StringOrDie().value());
  }
  {
    const auto value = wrapper[CelValue::CreateStringView(ListenerMetadata)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsMessage());
    EXPECT_EQ(&listener_metadata, value.value().MessageOrDie());
  }
  {
    const auto value = wrapper[CelValue::CreateStringView(ListenerDirection)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsInt64());
    EXPECT_EQ(2, value.value().Int64OrDie());
  }
  {
    const auto value = wrapper[CelValue::CreateStringView(XDS)];
    EXPECT_FALSE(value.has_value());
  }
  {
    const auto value = wrapper[CelValue::CreateInt64(5)];
    EXPECT_FALSE(value.has_value());
  }
  {
    const auto value = wrapper[CelValue::CreateStringView(Node)];
    EXPECT_TRUE(value.has_value());
    ASSERT_TRUE(value.value().IsMessage());
  }
}

TEST(Context, EmptyXdsWrapper) {
  Protobuf::Arena arena;
  XDSWrapper wrapper(arena, nullptr, nullptr);

  {
    const auto value = wrapper[CelValue::CreateStringView(Node)];
    EXPECT_FALSE(value.has_value());
  }

  {
    const auto value = wrapper[CelValue::CreateStringView(ClusterName)];
    EXPECT_FALSE(value.has_value());
  }
}

} // namespace
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
