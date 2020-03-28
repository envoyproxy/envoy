#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"

#include "common/access_log/access_log_formatter.h"
#include "common/common/utility.h"
#include "common/http/header_map_impl.h"
#include "common/protobuf/utility.h"
#include "common/router/string_accessor_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Const;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace AccessLog {
namespace {

class TestSerializedUnknownFilterState : public StreamInfo::FilterState::Object {
public:
  ProtobufTypes::MessagePtr serializeAsProto() const override {
    auto any = std::make_unique<ProtobufWkt::Any>();
    any->set_type_url("UnknownType");
    any->set_value("\xde\xad\xbe\xef");
    return any;
  }
};

class TestSerializedStructFilterState : public StreamInfo::FilterState::Object {
public:
  TestSerializedStructFilterState() : use_struct_(true) {
    (*struct_.mutable_fields())["inner_key"] = ValueUtil::stringValue("inner_value");
  }

  explicit TestSerializedStructFilterState(const ProtobufWkt::Struct& s) : use_struct_(true) {
    struct_.CopyFrom(s);
  }

  explicit TestSerializedStructFilterState(std::chrono::seconds seconds) {
    duration_.set_seconds(seconds.count());
  }

  ProtobufTypes::MessagePtr serializeAsProto() const override {
    if (use_struct_) {
      auto s = std::make_unique<ProtobufWkt::Struct>();
      s->CopyFrom(struct_);
      return s;
    }

    auto d = std::make_unique<ProtobufWkt::Duration>();
    d->CopyFrom(duration_);
    return d;
  }

private:
  const bool use_struct_{false};
  ProtobufWkt::Struct struct_;
  ProtobufWkt::Duration duration_;
};

TEST(AccessLogFormatUtilsTest, protocolToString) {
  EXPECT_EQ("HTTP/1.0", AccessLogFormatUtils::protocolToString(Http::Protocol::Http10));
  EXPECT_EQ("HTTP/1.1", AccessLogFormatUtils::protocolToString(Http::Protocol::Http11));
  EXPECT_EQ("HTTP/2", AccessLogFormatUtils::protocolToString(Http::Protocol::Http2));
  EXPECT_EQ("-", AccessLogFormatUtils::protocolToString({}));
}

TEST(AccessLogFormatterTest, plainStringFormatter) {
  PlainStringFormatter formatter("plain");
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  StreamInfo::MockStreamInfo stream_info;

  EXPECT_EQ("plain",
            formatter.format(request_headers, response_headers, response_trailers, stream_info));
  EXPECT_THAT(
      formatter.formatValue(request_headers, response_headers, response_trailers, stream_info),
      ProtoEq(ValueUtil::stringValue("plain")));
}

TEST(AccessLogFormatterTest, streamInfoFormatter) {
  EXPECT_THROW(StreamInfoFormatter formatter("unknown_field"), EnvoyException);

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;

  {
    StreamInfoFormatter request_duration_format("REQUEST_DURATION");
    absl::optional<std::chrono::nanoseconds> dur = std::chrono::nanoseconds(5000000);
    EXPECT_CALL(stream_info, lastDownstreamRxByteReceived()).WillRepeatedly(Return(dur));
    EXPECT_EQ("5", request_duration_format.format(request_headers, response_headers,
                                                  response_trailers, stream_info));
    EXPECT_THAT(request_duration_format.formatValue(request_headers, response_headers,
                                                    response_trailers, stream_info),
                ProtoEq(ValueUtil::numberValue(5.0)));
  }

  {
    StreamInfoFormatter request_duration_format("REQUEST_DURATION");
    absl::optional<std::chrono::nanoseconds> dur;
    EXPECT_CALL(stream_info, lastDownstreamRxByteReceived()).WillRepeatedly(Return(dur));
    EXPECT_EQ("-", request_duration_format.format(request_headers, response_headers,
                                                  response_trailers, stream_info));
    EXPECT_THAT(request_duration_format.formatValue(request_headers, response_headers,
                                                    response_trailers, stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }

  {
    StreamInfoFormatter response_duration_format("RESPONSE_DURATION");
    absl::optional<std::chrono::nanoseconds> dur = std::chrono::nanoseconds(10000000);
    EXPECT_CALL(stream_info, firstUpstreamRxByteReceived()).WillRepeatedly(Return(dur));
    EXPECT_EQ("10", response_duration_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info));
    EXPECT_THAT(response_duration_format.formatValue(request_headers, response_headers,
                                                     response_trailers, stream_info),
                ProtoEq(ValueUtil::numberValue(10.0)));
  }

  {
    StreamInfoFormatter response_duration_format("RESPONSE_DURATION");
    absl::optional<std::chrono::nanoseconds> dur;
    EXPECT_CALL(stream_info, firstUpstreamRxByteReceived()).WillRepeatedly(Return(dur));
    EXPECT_EQ("-", response_duration_format.format(request_headers, response_headers,
                                                   response_trailers, stream_info));
    EXPECT_THAT(response_duration_format.formatValue(request_headers, response_headers,
                                                     response_trailers, stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }

  {
    StreamInfoFormatter ttlb_duration_format("RESPONSE_TX_DURATION");

    absl::optional<std::chrono::nanoseconds> dur_upstream = std::chrono::nanoseconds(10000000);
    EXPECT_CALL(stream_info, firstUpstreamRxByteReceived()).WillRepeatedly(Return(dur_upstream));
    absl::optional<std::chrono::nanoseconds> dur_downstream = std::chrono::nanoseconds(25000000);
    EXPECT_CALL(stream_info, lastDownstreamTxByteSent()).WillRepeatedly(Return(dur_downstream));

    EXPECT_EQ("15", ttlb_duration_format.format(request_headers, response_headers,
                                                response_trailers, stream_info));
    EXPECT_THAT(ttlb_duration_format.formatValue(request_headers, response_headers,
                                                 response_trailers, stream_info),
                ProtoEq(ValueUtil::numberValue(15.0)));
  }

  {
    StreamInfoFormatter ttlb_duration_format("RESPONSE_TX_DURATION");

    absl::optional<std::chrono::nanoseconds> dur_upstream;
    EXPECT_CALL(stream_info, firstUpstreamRxByteReceived()).WillRepeatedly(Return(dur_upstream));
    absl::optional<std::chrono::nanoseconds> dur_downstream;
    EXPECT_CALL(stream_info, lastDownstreamTxByteSent()).WillRepeatedly(Return(dur_downstream));

    EXPECT_EQ("-", ttlb_duration_format.format(request_headers, response_headers, response_trailers,
                                               stream_info));
    EXPECT_THAT(ttlb_duration_format.formatValue(request_headers, response_headers,
                                                 response_trailers, stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }

  {
    StreamInfoFormatter bytes_received_format("BYTES_RECEIVED");
    EXPECT_CALL(stream_info, bytesReceived()).WillRepeatedly(Return(1));
    EXPECT_EQ("1", bytes_received_format.format(request_headers, response_headers,
                                                response_trailers, stream_info));
    EXPECT_THAT(bytes_received_format.formatValue(request_headers, response_headers,
                                                  response_trailers, stream_info),
                ProtoEq(ValueUtil::numberValue(1.0)));
  }

  {
    StreamInfoFormatter protocol_format("PROTOCOL");
    absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
    EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));
    EXPECT_EQ("HTTP/1.1", protocol_format.format(request_headers, response_headers,
                                                 response_trailers, stream_info));
    EXPECT_THAT(protocol_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("HTTP/1.1")));
  }

  {
    StreamInfoFormatter response_format("RESPONSE_CODE");
    absl::optional<uint32_t> response_code{200};
    EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(Return(response_code));
    EXPECT_EQ("200", response_format.format(request_headers, response_headers, response_trailers,
                                            stream_info));
    EXPECT_THAT(response_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::numberValue(200.0)));
  }

  {
    StreamInfoFormatter response_code_format("RESPONSE_CODE");
    absl::optional<uint32_t> response_code;
    EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(Return(response_code));
    EXPECT_EQ("0", response_code_format.format(request_headers, response_headers, response_trailers,
                                               stream_info));
    EXPECT_THAT(response_code_format.formatValue(request_headers, response_headers,
                                                 response_trailers, stream_info),
                ProtoEq(ValueUtil::numberValue(0.0)));
  }

  {
    StreamInfoFormatter response_format("RESPONSE_CODE_DETAILS");
    absl::optional<std::string> rc_details;
    EXPECT_CALL(stream_info, responseCodeDetails()).WillRepeatedly(ReturnRef(rc_details));
    EXPECT_EQ("-", response_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(response_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }

  {
    StreamInfoFormatter response_code_format("RESPONSE_CODE_DETAILS");
    absl::optional<std::string> rc_details{"via_upstream"};
    EXPECT_CALL(stream_info, responseCodeDetails()).WillRepeatedly(ReturnRef(rc_details));
    EXPECT_EQ("via_upstream", response_code_format.format(request_headers, response_headers,
                                                          response_trailers, stream_info));
    EXPECT_THAT(response_code_format.formatValue(request_headers, response_headers,
                                                 response_trailers, stream_info),
                ProtoEq(ValueUtil::stringValue("via_upstream")));
  }

  {
    StreamInfoFormatter bytes_sent_format("BYTES_SENT");
    EXPECT_CALL(stream_info, bytesSent()).WillRepeatedly(Return(1));
    EXPECT_EQ("1", bytes_sent_format.format(request_headers, response_headers, response_trailers,
                                            stream_info));
    EXPECT_THAT(bytes_sent_format.formatValue(request_headers, response_headers, response_trailers,
                                              stream_info),
                ProtoEq(ValueUtil::numberValue(1.0)));
  }

  {
    StreamInfoFormatter duration_format("DURATION");
    absl::optional<std::chrono::nanoseconds> dur = std::chrono::nanoseconds(15000000);
    EXPECT_CALL(stream_info, requestComplete()).WillRepeatedly(Return(dur));
    EXPECT_EQ("15", duration_format.format(request_headers, response_headers, response_trailers,
                                           stream_info));
    EXPECT_THAT(duration_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::numberValue(15.0)));
  }

  {
    StreamInfoFormatter response_flags_format("RESPONSE_FLAGS");
    ON_CALL(stream_info, hasResponseFlag(StreamInfo::ResponseFlag::LocalReset))
        .WillByDefault(Return(true));
    EXPECT_EQ("LR", response_flags_format.format(request_headers, response_headers,
                                                 response_trailers, stream_info));
    EXPECT_THAT(response_flags_format.formatValue(request_headers, response_headers,
                                                  response_trailers, stream_info),
                ProtoEq(ValueUtil::stringValue("LR")));
  }

  {
    StreamInfoFormatter upstream_format("UPSTREAM_HOST");
    EXPECT_EQ("10.0.0.1:443", upstream_format.format(request_headers, response_headers,
                                                     response_trailers, stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("10.0.0.1:443")));
  }

  {
    StreamInfoFormatter upstream_format("UPSTREAM_CLUSTER");
    const std::string upstream_cluster_name = "cluster_name";
    EXPECT_CALL(stream_info.host_->cluster_, name())
        .WillRepeatedly(ReturnRef(upstream_cluster_name));
    EXPECT_EQ("cluster_name", upstream_format.format(request_headers, response_headers,
                                                     response_trailers, stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("cluster_name")));
  }

  {
    StreamInfoFormatter upstream_format("UPSTREAM_HOST");
    EXPECT_CALL(stream_info, upstreamHost()).WillRepeatedly(Return(nullptr));
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }

  {
    NiceMock<Api::MockOsSysCalls> os_sys_calls;
    TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
    EXPECT_CALL(os_sys_calls, gethostname(_, _))
        .WillOnce(Invoke([](char*, size_t) -> Api::SysCallIntResult {
          return {-1, ENAMETOOLONG};
        }));

    StreamInfoFormatter upstream_format("HOSTNAME");
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("-")));
  }

  {
    NiceMock<Api::MockOsSysCalls> os_sys_calls;
    TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
    EXPECT_CALL(os_sys_calls, gethostname(_, _))
        .WillOnce(Invoke([](char* name, size_t) -> Api::SysCallIntResult {
          StringUtil::strlcpy(name, "myhostname", 11);
          return {0, 0};
        }));

    StreamInfoFormatter upstream_format("HOSTNAME");
    EXPECT_EQ("myhostname", upstream_format.format(request_headers, response_headers,
                                                   response_trailers, stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("myhostname")));
  }

  {
    StreamInfoFormatter upstream_format("UPSTREAM_CLUSTER");
    EXPECT_CALL(stream_info, upstreamHost()).WillRepeatedly(Return(nullptr));
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }

  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_ADDRESS");
    EXPECT_EQ("127.0.0.2:0", upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("127.0.0.2:0")));
  }

  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT");
    EXPECT_EQ("127.0.0.2", upstream_format.format(request_headers, response_headers,
                                                  response_trailers, stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("127.0.0.2")));
  }

  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_PORT");

    // Validate for IPv4 address
    auto address = Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv4Instance("127.1.2.3", 8443)};
    EXPECT_CALL(stream_info, downstreamLocalAddress()).WillRepeatedly(ReturnRef(address));
    EXPECT_EQ("8443", upstream_format.format(request_headers, response_headers, response_trailers,
                                             stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("8443")));

    // Validate for IPv6 address
    address =
        Network::Address::InstanceConstSharedPtr{new Network::Address::Ipv6Instance("::1", 9443)};
    EXPECT_CALL(stream_info, downstreamLocalAddress()).WillRepeatedly(ReturnRef(address));
    EXPECT_EQ("9443", upstream_format.format(request_headers, response_headers, response_trailers,
                                             stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("9443")));

    // Validate for Pipe
    address = Network::Address::InstanceConstSharedPtr{new Network::Address::PipeInstance("/foo")};
    EXPECT_CALL(stream_info, downstreamLocalAddress()).WillRepeatedly(ReturnRef(address));
    EXPECT_EQ("", upstream_format.format(request_headers, response_headers, response_trailers,
                                         stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("")));
  }

  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT");
    EXPECT_EQ("127.0.0.1", upstream_format.format(request_headers, response_headers,
                                                  response_trailers, stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("127.0.0.1")));
  }

  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_REMOTE_ADDRESS");
    EXPECT_EQ("127.0.0.1:0", upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("127.0.0.1:0")));
  }

  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT");
    EXPECT_EQ("127.0.0.1", upstream_format.format(request_headers, response_headers,
                                                  response_trailers, stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("127.0.0.1")));
  }

  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_DIRECT_REMOTE_ADDRESS");
    EXPECT_EQ("127.0.0.1:0", upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("127.0.0.1:0")));
  }

  {
    StreamInfoFormatter upstream_format("REQUESTED_SERVER_NAME");
    std::string requested_server_name = "stub_server";
    EXPECT_CALL(stream_info, requestedServerName())
        .WillRepeatedly(ReturnRef(requested_server_name));
    EXPECT_EQ("stub_server", upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("stub_server")));
  }

  {
    StreamInfoFormatter upstream_format("REQUESTED_SERVER_NAME");
    std::string requested_server_name;
    EXPECT_CALL(stream_info, requestedServerName())
        .WillRepeatedly(ReturnRef(requested_server_name));
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_URI_SAN");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::vector<std::string> sans{"san"};
    EXPECT_CALL(*connection_info, uriSanPeerCertificate()).WillRepeatedly(Return(sans));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("san", upstream_format.format(request_headers, response_headers, response_trailers,
                                            stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("san")));
  }

  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_URI_SAN");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::vector<std::string> sans{"san1", "san2"};
    EXPECT_CALL(*connection_info, uriSanPeerCertificate()).WillRepeatedly(Return(sans));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("san1,san2", upstream_format.format(request_headers, response_headers,
                                                  response_trailers, stream_info));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_URI_SAN");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, uriSanPeerCertificate())
        .WillRepeatedly(Return(std::vector<std::string>()));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_URI_SAN");
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_URI_SAN");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::vector<std::string> sans{"san"};
    EXPECT_CALL(*connection_info, uriSanLocalCertificate()).WillRepeatedly(Return(sans));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("san", upstream_format.format(request_headers, response_headers, response_trailers,
                                            stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("san")));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_URI_SAN");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::vector<std::string> sans{"san1", "san2"};
    EXPECT_CALL(*connection_info, uriSanLocalCertificate()).WillRepeatedly(Return(sans));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("san1,san2", upstream_format.format(request_headers, response_headers,
                                                  response_trailers, stream_info));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_URI_SAN");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, uriSanLocalCertificate())
        .WillRepeatedly(Return(std::vector<std::string>()));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_URI_SAN");
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_SUBJECT");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::string subject_local = "subject";
    EXPECT_CALL(*connection_info, subjectLocalCertificate())
        .WillRepeatedly(ReturnRef(subject_local));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("subject", upstream_format.format(request_headers, response_headers,
                                                response_trailers, stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("subject")));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_SUBJECT");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, subjectLocalCertificate())
        .WillRepeatedly(ReturnRef(EMPTY_STRING));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_SUBJECT");
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_SUBJECT");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::string subject_peer = "subject";
    EXPECT_CALL(*connection_info, subjectPeerCertificate()).WillRepeatedly(ReturnRef(subject_peer));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("subject", upstream_format.format(request_headers, response_headers,
                                                response_trailers, stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("subject")));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_SUBJECT");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, subjectPeerCertificate()).WillRepeatedly(ReturnRef(EMPTY_STRING));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_SUBJECT");
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_TLS_SESSION_ID");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::string session_id = "deadbeef";
    EXPECT_CALL(*connection_info, sessionId()).WillRepeatedly(ReturnRef(session_id));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("deadbeef", upstream_format.format(request_headers, response_headers,
                                                 response_trailers, stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("deadbeef")));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_TLS_SESSION_ID");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, sessionId()).WillRepeatedly(ReturnRef(EMPTY_STRING));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
    StreamInfoFormatter upstream_format("DOWNSTREAM_TLS_SESSION_ID");
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_TLS_CIPHER");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, ciphersuiteString())
        .WillRepeatedly(Return("TLS_DHE_RSA_WITH_AES_256_GCM_SHA384"));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ(
        "TLS_DHE_RSA_WITH_AES_256_GCM_SHA384",
        upstream_format.format(request_headers, response_headers, response_trailers, stream_info));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_TLS_CIPHER");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, ciphersuiteString()).WillRepeatedly(Return(""));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
    StreamInfoFormatter upstream_format("DOWNSTREAM_TLS_CIPHER");
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_TLS_VERSION");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    std::string tlsVersion = "TLSv1.2";
    EXPECT_CALL(*connection_info, tlsVersion()).WillRepeatedly(ReturnRef(tlsVersion));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("TLSv1.2", upstream_format.format(request_headers, response_headers,
                                                response_trailers, stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("TLSv1.2")));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_TLS_VERSION");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, tlsVersion()).WillRepeatedly(ReturnRef(EMPTY_STRING));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
    StreamInfoFormatter upstream_format("DOWNSTREAM_TLS_VERSION");
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_FINGERPRINT_256");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    std::string expected_sha = "685a2db593d5f86d346cb1a297009c3b467ad77f1944aa799039a2fb3d531f3f";
    EXPECT_CALL(*connection_info, sha256PeerCertificateDigest())
        .WillRepeatedly(ReturnRef(expected_sha));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ(expected_sha, upstream_format.format(request_headers, response_headers,
                                                   response_trailers, stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue(expected_sha)));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_FINGERPRINT_256");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    std::string expected_sha;
    EXPECT_CALL(*connection_info, sha256PeerCertificateDigest())
        .WillRepeatedly(ReturnRef(expected_sha));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_FINGERPRINT_256");
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_SERIAL");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::string serial_number = "b8b5ecc898f2124a";
    EXPECT_CALL(*connection_info, serialNumberPeerCertificate())
        .WillRepeatedly(ReturnRef(serial_number));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("b8b5ecc898f2124a", upstream_format.format(request_headers, response_headers,
                                                         response_trailers, stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("b8b5ecc898f2124a")));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_SERIAL");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, serialNumberPeerCertificate())
        .WillRepeatedly(ReturnRef(EMPTY_STRING));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_SERIAL");
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_ISSUER");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::string issuer_peer =
        "CN=Test CA,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US";
    EXPECT_CALL(*connection_info, issuerPeerCertificate()).WillRepeatedly(ReturnRef(issuer_peer));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ(
        "CN=Test CA,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US",
        upstream_format.format(request_headers, response_headers, response_trailers, stream_info));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_ISSUER");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, issuerPeerCertificate()).WillRepeatedly(ReturnRef(EMPTY_STRING));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_ISSUER");
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_SUBJECT");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::string subject_peer =
        "CN=Test Server,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US";
    EXPECT_CALL(*connection_info, subjectPeerCertificate()).WillRepeatedly(ReturnRef(subject_peer));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ(
        "CN=Test Server,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US",
        upstream_format.format(request_headers, response_headers, response_trailers, stream_info));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_SUBJECT");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, subjectPeerCertificate()).WillRepeatedly(ReturnRef(EMPTY_STRING));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_SUBJECT");
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_CERT");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    std::string expected_cert = "<some cert>";
    EXPECT_CALL(*connection_info, urlEncodedPemEncodedPeerCertificate())
        .WillRepeatedly(ReturnRef(expected_cert));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ(expected_cert, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue(expected_cert)));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_CERT");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    std::string expected_cert = "";
    EXPECT_CALL(*connection_info, urlEncodedPemEncodedPeerCertificate())
        .WillRepeatedly(ReturnRef(expected_cert));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_CERT");
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_CERT_V_START");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    absl::Time abslStartTime =
        TestUtility::parseTime("Dec 18 01:50:34 2018 GMT", "%b %e %H:%M:%S %Y GMT");
    SystemTime startTime = absl::ToChronoTime(abslStartTime);
    EXPECT_CALL(*connection_info, validFromPeerCertificate()).WillRepeatedly(Return(startTime));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("2018-12-18T01:50:34.000Z", upstream_format.format(request_headers, response_headers,
                                                                 response_trailers, stream_info));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_CERT_V_START");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, validFromPeerCertificate()).WillRepeatedly(Return(absl::nullopt));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_CERT_V_START");
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_CERT_V_END");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    absl::Time abslEndTime =
        TestUtility::parseTime("Dec 17 01:50:34 2020 GMT", "%b %e %H:%M:%S %Y GMT");
    SystemTime endTime = absl::ToChronoTime(abslEndTime);
    EXPECT_CALL(*connection_info, expirationPeerCertificate()).WillRepeatedly(Return(endTime));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("2020-12-17T01:50:34.000Z", upstream_format.format(request_headers, response_headers,
                                                                 response_trailers, stream_info));
  }
  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_CERT_V_END");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, expirationPeerCertificate())
        .WillRepeatedly(Return(absl::nullopt));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_CERT_V_END");
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    StreamInfoFormatter upstream_format("UPSTREAM_TRANSPORT_FAILURE_REASON");
    std::string upstream_transport_failure_reason = "SSL error";
    EXPECT_CALL(stream_info, upstreamTransportFailureReason())
        .WillRepeatedly(ReturnRef(upstream_transport_failure_reason));
    EXPECT_EQ("SSL error", upstream_format.format(request_headers, response_headers,
                                                  response_trailers, stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::stringValue("SSL error")));
  }

  {
    StreamInfoFormatter upstream_format("UPSTREAM_TRANSPORT_FAILURE_REASON");
    std::string upstream_transport_failure_reason;
    EXPECT_CALL(stream_info, upstreamTransportFailureReason())
        .WillRepeatedly(ReturnRef(upstream_transport_failure_reason));
    EXPECT_EQ("-", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info),
                ProtoEq(ValueUtil::nullValue()));
  }
}

TEST(AccessLogFormatterTest, requestHeaderFormatter) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{":method", "PUT"}};
  Http::TestResponseTrailerMapImpl response_trailer{{":method", "POST"}, {"test-2", "test-2"}};

  {
    RequestHeaderFormatter formatter(":Method", "", absl::optional<size_t>());
    EXPECT_EQ("GET",
              formatter.format(request_header, response_header, response_trailer, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info),
        ProtoEq(ValueUtil::stringValue("GET")));
  }

  {
    RequestHeaderFormatter formatter(":path", ":method", absl::optional<size_t>());
    EXPECT_EQ("/",
              formatter.format(request_header, response_header, response_trailer, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info),
        ProtoEq(ValueUtil::stringValue("/")));
  }

  {
    RequestHeaderFormatter formatter(":TEST", ":METHOD", absl::optional<size_t>());
    EXPECT_EQ("GET",
              formatter.format(request_header, response_header, response_trailer, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info),
        ProtoEq(ValueUtil::stringValue("GET")));
  }

  {
    RequestHeaderFormatter formatter("does_not_exist", "", absl::optional<size_t>());
    EXPECT_EQ("-",
              formatter.format(request_header, response_header, response_trailer, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info),
        ProtoEq(ValueUtil::nullValue()));
  }

  {
    RequestHeaderFormatter formatter(":Method", "", absl::optional<size_t>(2));
    EXPECT_EQ("GE",
              formatter.format(request_header, response_header, response_trailer, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info),
        ProtoEq(ValueUtil::stringValue("GE")));
  }
}

TEST(AccessLogFormatterTest, responseHeaderFormatter) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{":method", "PUT"}, {"test", "test"}};
  Http::TestResponseTrailerMapImpl response_trailer{{":method", "POST"}, {"test-2", "test-2"}};

  {
    ResponseHeaderFormatter formatter(":method", "", absl::optional<size_t>());
    EXPECT_EQ("PUT",
              formatter.format(request_header, response_header, response_trailer, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info),
        ProtoEq(ValueUtil::stringValue("PUT")));
  }

  {
    ResponseHeaderFormatter formatter("test", ":method", absl::optional<size_t>());
    EXPECT_EQ("test",
              formatter.format(request_header, response_header, response_trailer, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info),
        ProtoEq(ValueUtil::stringValue("test")));
  }

  {
    ResponseHeaderFormatter formatter(":path", ":method", absl::optional<size_t>());
    EXPECT_EQ("PUT",
              formatter.format(request_header, response_header, response_trailer, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info),
        ProtoEq(ValueUtil::stringValue("PUT")));
  }

  {
    ResponseHeaderFormatter formatter("does_not_exist", "", absl::optional<size_t>());
    EXPECT_EQ("-",
              formatter.format(request_header, response_header, response_trailer, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info),
        ProtoEq(ValueUtil::nullValue()));
  }

  {
    ResponseHeaderFormatter formatter(":method", "", absl::optional<size_t>(2));
    EXPECT_EQ("PU",
              formatter.format(request_header, response_header, response_trailer, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info),
        ProtoEq(ValueUtil::stringValue("PU")));
  }
}

TEST(AccessLogFormatterTest, responseTrailerFormatter) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{":method", "PUT"}, {"test", "test"}};
  Http::TestResponseTrailerMapImpl response_trailer{{":method", "POST"}, {"test-2", "test-2"}};

  {
    ResponseTrailerFormatter formatter(":method", "", absl::optional<size_t>());
    EXPECT_EQ("POST",
              formatter.format(request_header, response_header, response_trailer, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info),
        ProtoEq(ValueUtil::stringValue("POST")));
  }

  {
    ResponseTrailerFormatter formatter("test-2", ":method", absl::optional<size_t>());
    EXPECT_EQ("test-2",
              formatter.format(request_header, response_header, response_trailer, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info),
        ProtoEq(ValueUtil::stringValue("test-2")));
  }

  {
    ResponseTrailerFormatter formatter(":path", ":method", absl::optional<size_t>());
    EXPECT_EQ("POST",
              formatter.format(request_header, response_header, response_trailer, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info),
        ProtoEq(ValueUtil::stringValue("POST")));
  }

  {
    ResponseTrailerFormatter formatter("does_not_exist", "", absl::optional<size_t>());
    EXPECT_EQ("-",
              formatter.format(request_header, response_header, response_trailer, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info),
        ProtoEq(ValueUtil::nullValue()));
  }

  {
    ResponseTrailerFormatter formatter(":method", "", absl::optional<size_t>(2));
    EXPECT_EQ("PO",
              formatter.format(request_header, response_header, response_trailer, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info),
        ProtoEq(ValueUtil::stringValue("PO")));
  }
}

/**
 * Populate a metadata object with the following test data:
 * "com.test": {"test_key":"test_value","test_obj":{"inner_key":"inner_value"}}
 */
void populateMetadataTestData(envoy::config::core::v3::Metadata& metadata) {
  ProtobufWkt::Struct struct_obj;
  auto& fields_map = *struct_obj.mutable_fields();
  fields_map["test_key"] = ValueUtil::stringValue("test_value");
  ProtobufWkt::Struct struct_inner;
  (*struct_inner.mutable_fields())["inner_key"] = ValueUtil::stringValue("inner_value");
  ProtobufWkt::Value val;
  *val.mutable_struct_value() = struct_inner;
  fields_map["test_obj"] = val;
  (*metadata.mutable_filter_metadata())["com.test"] = struct_obj;
}

TEST(AccessLogFormatterTest, DynamicMetadataFormatter) {
  envoy::config::core::v3::Metadata metadata;
  populateMetadataTestData(metadata);
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(stream_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
  EXPECT_CALL(Const(stream_info), dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;

  {
    DynamicMetadataFormatter formatter("com.test", {}, absl::optional<size_t>());
    std::string val =
        formatter.format(request_headers, response_headers, response_trailers, stream_info);
    EXPECT_TRUE(val.find("\"test_key\":\"test_value\"") != std::string::npos);
    EXPECT_TRUE(val.find("\"test_obj\":{\"inner_key\":\"inner_value\"}") != std::string::npos);

    ProtobufWkt::Value expected_val;
    expected_val.mutable_struct_value()->CopyFrom(metadata.filter_metadata().at("com.test"));
    EXPECT_THAT(
        formatter.formatValue(request_headers, response_headers, response_trailers, stream_info),
        ProtoEq(expected_val));
  }
  {
    DynamicMetadataFormatter formatter("com.test", {"test_key"}, absl::optional<size_t>());
    EXPECT_EQ("\"test_value\"",
              formatter.format(request_headers, response_headers, response_trailers, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_headers, response_headers, response_trailers, stream_info),
        ProtoEq(ValueUtil::stringValue("test_value")));
  }
  {
    DynamicMetadataFormatter formatter("com.test", {"test_obj"}, absl::optional<size_t>());
    EXPECT_EQ("{\"inner_key\":\"inner_value\"}",
              formatter.format(request_headers, response_headers, response_trailers, stream_info));

    ProtobufWkt::Value expected_val;
    (*expected_val.mutable_struct_value()->mutable_fields())["inner_key"] =
        ValueUtil::stringValue("inner_value");
    EXPECT_THAT(
        formatter.formatValue(request_headers, response_headers, response_trailers, stream_info),
        ProtoEq(expected_val));
  }
  {
    DynamicMetadataFormatter formatter("com.test", {"test_obj", "inner_key"},
                                       absl::optional<size_t>());
    EXPECT_EQ("\"inner_value\"",
              formatter.format(request_headers, response_headers, response_trailers, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_headers, response_headers, response_trailers, stream_info),
        ProtoEq(ValueUtil::stringValue("inner_value")));
  }

  // not found cases
  {
    DynamicMetadataFormatter formatter("com.notfound", {}, absl::optional<size_t>());
    EXPECT_EQ("-",
              formatter.format(request_headers, response_headers, response_trailers, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_headers, response_headers, response_trailers, stream_info),
        ProtoEq(ValueUtil::nullValue()));
  }
  {
    DynamicMetadataFormatter formatter("com.test", {"notfound"}, absl::optional<size_t>());
    EXPECT_EQ("-",
              formatter.format(request_headers, response_headers, response_trailers, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_headers, response_headers, response_trailers, stream_info),
        ProtoEq(ValueUtil::nullValue()));
  }
  {
    DynamicMetadataFormatter formatter("com.test", {"test_obj", "notfound"},
                                       absl::optional<size_t>());
    EXPECT_EQ("-",
              formatter.format(request_headers, response_headers, response_trailers, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_headers, response_headers, response_trailers, stream_info),
        ProtoEq(ValueUtil::nullValue()));
  }

  // size limit
  {
    DynamicMetadataFormatter formatter("com.test", {"test_key"}, absl::optional<size_t>(5));
    EXPECT_EQ("\"test",
              formatter.format(request_headers, response_headers, response_trailers, stream_info));

    // N.B. Does not truncate.
    EXPECT_THAT(
        formatter.formatValue(request_headers, response_headers, response_trailers, stream_info),
        ProtoEq(ValueUtil::stringValue("test_value")));
  }
}

TEST(AccessLogFormatterTest, FilterStateFormatter) {
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  StreamInfo::MockStreamInfo stream_info;
  stream_info.filter_state_->setData("key",
                                     std::make_unique<Router::StringAccessorImpl>("test_value"),
                                     StreamInfo::FilterState::StateType::ReadOnly);
  stream_info.filter_state_->setData("key-struct",
                                     std::make_unique<TestSerializedStructFilterState>(),
                                     StreamInfo::FilterState::StateType::ReadOnly);
  stream_info.filter_state_->setData("key-no-serialization",
                                     std::make_unique<StreamInfo::FilterState::Object>(),
                                     StreamInfo::FilterState::StateType::ReadOnly);
  stream_info.filter_state_->setData(
      "key-serialization-error",
      std::make_unique<TestSerializedStructFilterState>(std::chrono::seconds(-281474976710656)),
      StreamInfo::FilterState::StateType::ReadOnly);
  EXPECT_CALL(Const(stream_info), filterState()).Times(testing::AtLeast(1));

  {
    FilterStateFormatter formatter("key", absl::optional<size_t>());

    EXPECT_EQ("\"test_value\"",
              formatter.format(request_headers, response_headers, response_trailers, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_headers, response_headers, response_trailers, stream_info),
        ProtoEq(ValueUtil::stringValue("test_value")));
  }
  {
    FilterStateFormatter formatter("key-struct", absl::optional<size_t>());

    EXPECT_EQ("{\"inner_key\":\"inner_value\"}",
              formatter.format(request_headers, response_headers, response_trailers, stream_info));

    ProtobufWkt::Value expected;
    (*expected.mutable_struct_value()->mutable_fields())["inner_key"] =
        ValueUtil::stringValue("inner_value");

    EXPECT_THAT(
        formatter.formatValue(request_headers, response_headers, response_trailers, stream_info),
        ProtoEq(expected));
  }

  // not found case
  {
    FilterStateFormatter formatter("key-not-found", absl::optional<size_t>());

    EXPECT_EQ("-",
              formatter.format(request_headers, response_headers, response_trailers, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_headers, response_headers, response_trailers, stream_info),
        ProtoEq(ValueUtil::nullValue()));
  }

  // no serialization case
  {
    FilterStateFormatter formatter("key-no-serialization", absl::optional<size_t>());

    EXPECT_EQ("-",
              formatter.format(request_headers, response_headers, response_trailers, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_headers, response_headers, response_trailers, stream_info),
        ProtoEq(ValueUtil::nullValue()));
  }

  // serialization error case
  {
    FilterStateFormatter formatter("key-serialization-error", absl::optional<size_t>());

    EXPECT_EQ("-",
              formatter.format(request_headers, response_headers, response_trailers, stream_info));
    EXPECT_THAT(
        formatter.formatValue(request_headers, response_headers, response_trailers, stream_info),
        ProtoEq(ValueUtil::nullValue()));
  }

  // size limit
  {
    FilterStateFormatter formatter("key", absl::optional<size_t>(5));

    EXPECT_EQ("\"test",
              formatter.format(request_headers, response_headers, response_trailers, stream_info));

    // N.B. Does not truncate.
    EXPECT_THAT(
        formatter.formatValue(request_headers, response_headers, response_trailers, stream_info),
        ProtoEq(ValueUtil::stringValue("test_value")));
  }
}

TEST(AccessLogFormatterTest, StartTimeFormatter) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;

  {
    StartTimeFormatter start_time_format("%Y/%m/%d");
    time_t test_epoch = 1522280158;
    SystemTime time = std::chrono::system_clock::from_time_t(test_epoch);
    EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(time));
    EXPECT_EQ("2018/03/28", start_time_format.format(request_headers, response_headers,
                                                     response_trailers, stream_info));
    EXPECT_THAT(start_time_format.formatValue(request_headers, response_headers, response_trailers,
                                              stream_info),
                ProtoEq(ValueUtil::stringValue("2018/03/28")));
  }

  {
    StartTimeFormatter start_time_format("");
    SystemTime time;
    EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(time));
    EXPECT_EQ(AccessLogDateTimeFormatter::fromTime(time),
              start_time_format.format(request_headers, response_headers, response_trailers,
                                       stream_info));
    EXPECT_THAT(start_time_format.formatValue(request_headers, response_headers, response_trailers,
                                              stream_info),
                ProtoEq(ValueUtil::stringValue(AccessLogDateTimeFormatter::fromTime(time))));
  }
}

void verifyJsonOutput(std::string json_string,
                      std::unordered_map<std::string, std::string> expected_map) {
  const auto parsed = Json::Factory::loadFromString(json_string);

  // Every json log line should have only one newline character, and it should be the last character
  // in the string
  const auto newline_pos = json_string.find('\n');
  EXPECT_NE(newline_pos, std::string::npos);
  EXPECT_EQ(newline_pos, json_string.length() - 1);

  for (const auto& pair : expected_map) {
    EXPECT_EQ(parsed->getString(pair.first), pair.second);
  }
}

TEST(AccessLogFormatterTest, JsonFormatterPlainStringTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header;
  Http::TestResponseHeaderMapImpl response_header;
  Http::TestResponseTrailerMapImpl response_trailer;

  envoy::config::core::v3::Metadata metadata;
  populateMetadataTestData(metadata);
  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  std::unordered_map<std::string, std::string> expected_json_map = {
      {"plain_string", "plain_string_value"}};

  std::unordered_map<std::string, std::string> key_mapping = {
      {"plain_string", "plain_string_value"}};
  JsonFormatterImpl formatter(key_mapping, false);

  verifyJsonOutput(formatter.format(request_header, response_header, response_trailer, stream_info),
                   expected_json_map);
}

TEST(AccessLogFormatterTest, JsonFormatterSingleOperatorTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header;
  Http::TestResponseHeaderMapImpl response_header;
  Http::TestResponseTrailerMapImpl response_trailer;

  envoy::config::core::v3::Metadata metadata;
  populateMetadataTestData(metadata);
  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  std::unordered_map<std::string, std::string> expected_json_map = {{"protocol", "HTTP/1.1"}};

  std::unordered_map<std::string, std::string> key_mapping = {{"protocol", "%PROTOCOL%"}};
  JsonFormatterImpl formatter(key_mapping, false);

  verifyJsonOutput(formatter.format(request_header, response_header, response_trailer, stream_info),
                   expected_json_map);
}

TEST(AccessLogFormatterTest, JsonFormatterNonExistentHeaderTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{"some_request_header", "SOME_REQUEST_HEADER"}};
  Http::TestResponseHeaderMapImpl response_header{{"some_response_header", "SOME_RESPONSE_HEADER"}};
  Http::TestResponseTrailerMapImpl response_trailer;

  std::unordered_map<std::string, std::string> expected_json_map = {
      {"protocol", "HTTP/1.1"},
      {"some_request_header", "SOME_REQUEST_HEADER"},
      {"nonexistent_response_header", "-"},
      {"some_response_header", "SOME_RESPONSE_HEADER"}};

  std::unordered_map<std::string, std::string> key_mapping = {
      {"protocol", "%PROTOCOL%"},
      {"some_request_header", "%REQ(some_request_header)%"},
      {"nonexistent_response_header", "%RESP(nonexistent_response_header)%"},
      {"some_response_header", "%RESP(some_response_header)%"}};
  JsonFormatterImpl formatter(key_mapping, false);

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  verifyJsonOutput(formatter.format(request_header, response_header, response_trailer, stream_info),
                   expected_json_map);
}

TEST(AccessLogFormatterTest, JsonFormatterAlternateHeaderTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{
      {"request_present_header", "REQUEST_PRESENT_HEADER"}};
  Http::TestResponseHeaderMapImpl response_header{
      {"response_present_header", "RESPONSE_PRESENT_HEADER"}};
  Http::TestResponseTrailerMapImpl response_trailer;

  std::unordered_map<std::string, std::string> expected_json_map = {
      {"request_present_header_or_request_absent_header", "REQUEST_PRESENT_HEADER"},
      {"request_absent_header_or_request_present_header", "REQUEST_PRESENT_HEADER"},
      {"response_absent_header_or_response_absent_header", "RESPONSE_PRESENT_HEADER"},
      {"response_present_header_or_response_absent_header", "RESPONSE_PRESENT_HEADER"}};

  std::unordered_map<std::string, std::string> key_mapping = {
      {"request_present_header_or_request_absent_header",
       "%REQ(request_present_header?request_absent_header)%"},
      {"request_absent_header_or_request_present_header",
       "%REQ(request_absent_header?request_present_header)%"},
      {"response_absent_header_or_response_absent_header",
       "%RESP(response_absent_header?response_present_header)%"},
      {"response_present_header_or_response_absent_header",
       "%RESP(response_present_header?response_absent_header)%"}};
  JsonFormatterImpl formatter(key_mapping, false);

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  verifyJsonOutput(formatter.format(request_header, response_header, response_trailer, stream_info),
                   expected_json_map);
}

TEST(AccessLogFormatterTest, JsonFormatterDynamicMetadataTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{"first", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{"second", "PUT"}, {"test", "test"}};
  Http::TestResponseTrailerMapImpl response_trailer{{"third", "POST"}, {"test-2", "test-2"}};

  envoy::config::core::v3::Metadata metadata;
  populateMetadataTestData(metadata);
  EXPECT_CALL(stream_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
  EXPECT_CALL(Const(stream_info), dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));

  std::unordered_map<std::string, std::string> expected_json_map = {
      {"test_key", "\"test_value\""},
      {"test_obj", "{\"inner_key\":\"inner_value\"}"},
      {"test_obj.inner_key", "\"inner_value\""}};

  std::unordered_map<std::string, std::string> key_mapping = {
      {"test_key", "%DYNAMIC_METADATA(com.test:test_key)%"},
      {"test_obj", "%DYNAMIC_METADATA(com.test:test_obj)%"},
      {"test_obj.inner_key", "%DYNAMIC_METADATA(com.test:test_obj:inner_key)%"}};

  JsonFormatterImpl formatter(key_mapping, false);

  verifyJsonOutput(formatter.format(request_header, response_header, response_trailer, stream_info),
                   expected_json_map);
}

TEST(AccessLogFormatterTest, JsonFormatterTypedDynamicMetadataTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{"first", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{"second", "PUT"}, {"test", "test"}};
  Http::TestResponseTrailerMapImpl response_trailer{{"third", "POST"}, {"test-2", "test-2"}};

  envoy::config::core::v3::Metadata metadata;
  populateMetadataTestData(metadata);
  EXPECT_CALL(stream_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
  EXPECT_CALL(Const(stream_info), dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));

  std::unordered_map<std::string, std::string> key_mapping = {
      {"test_key", "%DYNAMIC_METADATA(com.test:test_key)%"},
      {"test_obj", "%DYNAMIC_METADATA(com.test:test_obj)%"},
      {"test_obj.inner_key", "%DYNAMIC_METADATA(com.test:test_obj:inner_key)%"}};

  JsonFormatterImpl formatter(key_mapping, true);

  const std::string json =
      formatter.format(request_header, response_header, response_trailer, stream_info);
  ProtobufWkt::Struct output;
  MessageUtil::loadFromJson(json, output);

  const auto& fields = output.fields();
  EXPECT_EQ("test_value", fields.at("test_key").string_value());
  EXPECT_EQ("inner_value", fields.at("test_obj.inner_key").string_value());
  EXPECT_EQ("inner_value",
            fields.at("test_obj").struct_value().fields().at("inner_key").string_value());
}

TEST(AccessLogFormatterTets, JsonFormatterFilterStateTest) {
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  StreamInfo::MockStreamInfo stream_info;
  stream_info.filter_state_->setData("test_key",
                                     std::make_unique<Router::StringAccessorImpl>("test_value"),
                                     StreamInfo::FilterState::StateType::ReadOnly);
  stream_info.filter_state_->setData("test_obj",
                                     std::make_unique<TestSerializedStructFilterState>(),
                                     StreamInfo::FilterState::StateType::ReadOnly);
  EXPECT_CALL(Const(stream_info), filterState()).Times(testing::AtLeast(1));

  std::unordered_map<std::string, std::string> expected_json_map = {
      {"test_key", "\"test_value\""}, {"test_obj", "{\"inner_key\":\"inner_value\"}"}};

  std::unordered_map<std::string, std::string> key_mapping = {
      {"test_key", "%FILTER_STATE(test_key)%"}, {"test_obj", "%FILTER_STATE(test_obj)%"}};

  JsonFormatterImpl formatter(key_mapping, false);

  verifyJsonOutput(
      formatter.format(request_headers, response_headers, response_trailers, stream_info),
      expected_json_map);
}

TEST(AccessLogFormatterTets, JsonFormatterTypedFilterStateTest) {
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  StreamInfo::MockStreamInfo stream_info;
  stream_info.filter_state_->setData("test_key",
                                     std::make_unique<Router::StringAccessorImpl>("test_value"),
                                     StreamInfo::FilterState::StateType::ReadOnly);
  stream_info.filter_state_->setData("test_obj",
                                     std::make_unique<TestSerializedStructFilterState>(),
                                     StreamInfo::FilterState::StateType::ReadOnly);
  EXPECT_CALL(Const(stream_info), filterState()).Times(testing::AtLeast(1));

  std::unordered_map<std::string, std::string> key_mapping = {
      {"test_key", "%FILTER_STATE(test_key)%"}, {"test_obj", "%FILTER_STATE(test_obj)%"}};

  JsonFormatterImpl formatter(key_mapping, true);

  std::string json =
      formatter.format(request_headers, response_headers, response_trailers, stream_info);
  ProtobufWkt::Struct output;
  MessageUtil::loadFromJson(json, output);

  const auto& fields = output.fields();
  EXPECT_EQ("test_value", fields.at("test_key").string_value());
  EXPECT_EQ("inner_value",
            fields.at("test_obj").struct_value().fields().at("inner_key").string_value());
}

TEST(AccessLogFormatterTest, JsonFormatterStartTimeTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header;
  Http::TestResponseHeaderMapImpl response_header;
  Http::TestResponseTrailerMapImpl response_trailer;

  time_t expected_time_in_epoch = 1522280158;
  SystemTime time = std::chrono::system_clock::from_time_t(expected_time_in_epoch);
  EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(time));

  std::unordered_map<std::string, std::string> expected_json_map = {
      {"simple_date", "2018/03/28"},
      {"test_time", fmt::format("{}", expected_time_in_epoch)},
      {"bad_format", "bad_format"},
      {"default", "2018-03-28T23:35:58.000Z"},
      {"all_zeroes", "000000000.0.00.000"}};

  std::unordered_map<std::string, std::string> key_mapping = {
      {"simple_date", "%START_TIME(%Y/%m/%d)%"},
      {"test_time", "%START_TIME(%s)%"},
      {"bad_format", "%START_TIME(bad_format)%"},
      {"default", "%START_TIME%"},
      {"all_zeroes", "%START_TIME(%f.%1f.%2f.%3f)%"}};
  JsonFormatterImpl formatter(key_mapping, false);

  verifyJsonOutput(formatter.format(request_header, response_header, response_trailer, stream_info),
                   expected_json_map);
}

TEST(AccessLogFormatterTest, JsonFormatterMultiTokenTest) {
  {
    StreamInfo::MockStreamInfo stream_info;
    Http::TestRequestHeaderMapImpl request_header{{"some_request_header", "SOME_REQUEST_HEADER"}};
    Http::TestResponseHeaderMapImpl response_header{
        {"some_response_header", "SOME_RESPONSE_HEADER"}};
    Http::TestResponseTrailerMapImpl response_trailer;

    std::unordered_map<std::string, std::string> expected_json_map = {
        {"multi_token_field", "HTTP/1.1 plainstring SOME_REQUEST_HEADER SOME_RESPONSE_HEADER"}};

    std::unordered_map<std::string, std::string> key_mapping = {
        {"multi_token_field",
         "%PROTOCOL% plainstring %REQ(some_request_header)% %RESP(some_response_header)%"}};

    for (const bool preserve_types : {false, true}) {
      JsonFormatterImpl formatter(key_mapping, preserve_types);

      absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
      EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

      const auto parsed = Json::Factory::loadFromString(
          formatter.format(request_header, response_header, response_trailer, stream_info));
      for (const auto& pair : expected_json_map) {
        EXPECT_EQ(parsed->getString(pair.first), pair.second);
      }
    }
  }
}

TEST(AccessLogFormatterTest, JsonFormatterTypedTest) {
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(Const(stream_info), lastDownstreamRxByteReceived())
      .WillRepeatedly(Return(std::chrono::nanoseconds(5000000)));

  ProtobufWkt::Value list;
  list.mutable_list_value()->add_values()->set_bool_value(true);
  list.mutable_list_value()->add_values()->set_string_value("two");
  list.mutable_list_value()->add_values()->set_number_value(3.14);

  ProtobufWkt::Struct s;
  (*s.mutable_fields())["list"] = list;

  stream_info.filter_state_->setData("test_obj",
                                     std::make_unique<TestSerializedStructFilterState>(s),
                                     StreamInfo::FilterState::StateType::ReadOnly);
  EXPECT_CALL(Const(stream_info), filterState()).Times(testing::AtLeast(1));

  std::unordered_map<std::string, std::string> key_mapping = {
      {"request_duration", "%REQUEST_DURATION%"},
      {"request_duration_multi", "%REQUEST_DURATION%ms"},
      {"filter_state", "%FILTER_STATE(test_obj)%"},
  };

  JsonFormatterImpl formatter(key_mapping, true);

  const auto json =
      formatter.format(request_headers, response_headers, response_trailers, stream_info);
  ProtobufWkt::Struct output;
  MessageUtil::loadFromJson(json, output);

  EXPECT_THAT(output.fields().at("request_duration"), ProtoEq(ValueUtil::numberValue(5.0)));
  EXPECT_THAT(output.fields().at("request_duration_multi"), ProtoEq(ValueUtil::stringValue("5ms")));

  ProtobufWkt::Value expected;
  expected.mutable_struct_value()->CopyFrom(s);
  EXPECT_THAT(output.fields().at("filter_state"), ProtoEq(expected));
}

TEST(AccessLogFormatterTest, CompositeFormatterSuccess) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{"first", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{"second", "PUT"}, {"test", "test"}};
  Http::TestResponseTrailerMapImpl response_trailer{{"third", "POST"}, {"test-2", "test-2"}};

  {
    const std::string format = "{{%PROTOCOL%}}   %RESP(not exist)%++%RESP(test)% "
                               "%REQ(FIRST?SECOND)% %RESP(FIRST?SECOND)%"
                               "\t@%TRAILER(THIRD)%@\t%TRAILER(TEST?TEST-2)%[]";
    FormatterImpl formatter(format);

    absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
    EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

    EXPECT_EQ("{{HTTP/1.1}}   -++test GET PUT\t@POST@\ttest-2[]",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    const std::string format = "{}*JUST PLAIN string]";
    FormatterImpl formatter(format);

    EXPECT_EQ(format,
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    const std::string format = "%REQ(first):3%|%REQ(first):1%|%RESP(first?second):2%|%REQ(first):"
                               "10%|%TRAILER(second?third):3%";

    FormatterImpl formatter(format);

    EXPECT_EQ("GET|G|PU|GET|POS",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    envoy::config::core::v3::Metadata metadata;
    populateMetadataTestData(metadata);
    EXPECT_CALL(stream_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
    EXPECT_CALL(Const(stream_info), dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
    const std::string format = "%DYNAMIC_METADATA(com.test:test_key)%|%DYNAMIC_METADATA(com.test:"
                               "test_obj)%|%DYNAMIC_METADATA(com.test:test_obj:inner_key)%";
    FormatterImpl formatter(format);

    EXPECT_EQ("\"test_value\"|{\"inner_key\":\"inner_value\"}|\"inner_value\"",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    EXPECT_CALL(Const(stream_info), filterState()).Times(testing::AtLeast(1));
    stream_info.filter_state_->setData("testing",
                                       std::make_unique<Router::StringAccessorImpl>("test_value"),
                                       StreamInfo::FilterState::StateType::ReadOnly,
                                       StreamInfo::FilterState::LifeSpan::FilterChain);
    stream_info.filter_state_->setData("serialized",
                                       std::make_unique<TestSerializedUnknownFilterState>(),
                                       StreamInfo::FilterState::StateType::ReadOnly,
                                       StreamInfo::FilterState::LifeSpan::FilterChain);
    const std::string format = "%FILTER_STATE(testing)%|%FILTER_STATE(serialized)%|"
                               "%FILTER_STATE(testing):8%|%FILTER_STATE(nonexisting)%";
    FormatterImpl formatter(format);

    EXPECT_EQ("\"test_value\"|-|\"test_va|-",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    const std::string format = "%START_TIME(%Y/%m/%d)%|%START_TIME(%s)%|%START_TIME(bad_format)%|"
                               "%START_TIME%|%START_TIME(%f.%1f.%2f.%3f)%";

    time_t expected_time_in_epoch = 1522280158;
    SystemTime time = std::chrono::system_clock::from_time_t(expected_time_in_epoch);
    EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(time));
    FormatterImpl formatter(format);

    EXPECT_EQ(fmt::format("2018/03/28|{}|bad_format|2018-03-28T23:35:58.000Z|000000000.0.00.000",
                          expected_time_in_epoch),
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    // This tests the beginning of time.
    const std::string format = "%START_TIME(%Y/%m/%d)%|%START_TIME(%s)%|%START_TIME(bad_format)%|"
                               "%START_TIME%|%START_TIME(%f.%1f.%2f.%3f)%";

    const time_t test_epoch = 0;
    const SystemTime time = std::chrono::system_clock::from_time_t(test_epoch);
    EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(time));
    FormatterImpl formatter(format);

    EXPECT_EQ("1970/01/01|0|bad_format|1970-01-01T00:00:00.000Z|000000000.0.00.000",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    // This tests multiple START_TIMEs.
    const std::string format =
        "%START_TIME(%s.%3f)%|%START_TIME(%s.%4f)%|%START_TIME(%s.%5f)%|%START_TIME(%s.%6f)%";
    const SystemTime start_time(std::chrono::microseconds(1522796769123456));
    EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(start_time));
    FormatterImpl formatter(format);
    EXPECT_EQ("1522796769.123|1522796769.1234|1522796769.12345|1522796769.123456",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    const std::string format =
        "%START_TIME(segment1:%s.%3f|segment2:%s.%4f|seg3:%s.%6f|%s-%3f-asdf-%9f|.%7f:segm5:%Y)%";
    const SystemTime start_time(std::chrono::microseconds(1522796769123456));
    EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(start_time));
    FormatterImpl formatter(format);
    EXPECT_EQ("segment1:1522796769.123|segment2:1522796769.1234|seg3:1522796769.123456|1522796769-"
              "123-asdf-123456000|.1234560:segm5:2018",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    // This tests START_TIME specifier that has shorter segments when formatted, i.e.
    // absl::FormatTime("%%%%"") equals "%%", %1f will have 1 as its size.
    const std::string format = "%START_TIME(%%%%|%%%%%f|%s%%%%%3f|%1f%%%%%s)%";
    const SystemTime start_time(std::chrono::microseconds(1522796769123456));
    EXPECT_CALL(stream_info, startTime()).WillOnce(Return(start_time));
    FormatterImpl formatter(format);
    EXPECT_EQ("%%|%%123456000|1522796769%%123|1%%1522796769",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }
}

TEST(AccessLogFormatterTest, ParserFailures) {
  AccessLogFormatParser parser;

  std::vector<std::string> test_cases = {
      "{{%PROTOCOL%}}   ++ %REQ(FIRST?SECOND)% %RESP(FIRST?SECOND)",
      "%REQ(FIRST?SECOND)T%",
      "RESP(FIRST)%",
      "%REQ(valid)% %NOT_VALID%",
      "%REQ(FIRST?SECOND%",
      "%%",
      "%protocol%",
      "%REQ(TEST):%",
      "%REQ(TEST):3q4%",
      "%REQ(\n)%",
      "%REQ(?\n)%",
      "%RESP(TEST):%",
      "%RESP(X?Y):%",
      "%RESP(X?Y):343o24%",
      "%REQ(TEST):10",
      "REQ(:TEST):10%",
      "%REQ(TEST:10%",
      "%REQ(",
      "%REQ(X?Y?Z)%",
      "%TRAILER(TEST):%",
      "%TRAILER(TEST):23u1%",
      "%TRAILER(X?Y?Z)%",
      "%TRAILER(:TEST):10",
      "%DYNAMIC_METADATA(TEST",
      "%FILTER_STATE(TEST",
      "%FILTER_STATE()%",
      "%START_TIME(%85n)%",
      "%START_TIME(%#__88n)%"};

  for (const std::string& test_case : test_cases) {
    EXPECT_THROW(parser.parse(test_case), EnvoyException) << test_case;
  }
}

} // namespace
} // namespace AccessLog
} // namespace Envoy
