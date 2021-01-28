#include <memory>

#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"

#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/logs/v1/logs.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"

#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/network/address_impl.h"
#include "common/protobuf/protobuf.h"
#include "common/router/string_accessor_impl.h"

#include "extensions/access_loggers/open_telemetry/access_log_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/thread_local/mocks.h"

using namespace std::chrono_literals;
using ::Envoy::AccessLog::FilterPtr;
using ::Envoy::AccessLog::MockFilter;
using testing::_;
using testing::An;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {
namespace {

using opentelemetry::proto::logs::v1::LogRecord;

class MockGrpcAccessLogger : public GrpcAccessLogger {
public:
  // GrpcAccessLogger
  MOCK_METHOD(void, log, (LogRecord && entry));
  MOCK_METHOD(void, log, (ProtobufWkt::Empty && entry));
};

class MockGrpcAccessLoggerCache : public GrpcAccessLoggerCache {
public:
  // GrpcAccessLoggerCache
  MOCK_METHOD(GrpcAccessLoggerSharedPtr, getOrCreateLogger,
              (const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
               Common::GrpcAccessLoggerType logger_type, Stats::Scope& scope));
};

class OtGrpcAccessLogTest : public testing::Test {
public:
  void init() {
    ON_CALL(*filter_, evaluate(_, _, _, _)).WillByDefault(Return(true));
    config_.mutable_common_config()->set_log_name("hello_log");
    EXPECT_CALL(*logger_cache_, getOrCreateLogger(_, _, _))
        .WillOnce(
            [this](const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig&
                       config,
                   Common::GrpcAccessLoggerType logger_type, Stats::Scope&) {
              EXPECT_EQ(config.DebugString(), config_.common_config().DebugString());
              EXPECT_EQ(Common::GrpcAccessLoggerType::HTTP, logger_type);
              return logger_;
            });
    access_log_ =
        std::make_unique<AccessLog>(FilterPtr{filter_}, config_, tls_, logger_cache_, scope_);
  }

  void expectLog(const std::string& expected_log_entry_yaml) {
    if (access_log_ == nullptr) {
      init();
    }

    LogRecord expected_log_entry;
    TestUtility::loadFromYaml(expected_log_entry_yaml, expected_log_entry);
    EXPECT_CALL(*logger_, log(An<LogRecord&&>()))
        .WillOnce(Invoke([expected_log_entry](LogRecord&& entry) {
          EXPECT_EQ(entry.DebugString(), expected_log_entry.DebugString());
        }));
  }

  Stats::IsolatedStoreImpl scope_;
  MockFilter* filter_{new NiceMock<MockFilter>()};
  NiceMock<ThreadLocal::MockInstance> tls_;
  envoy::extensions::access_loggers::grpc::v3::HttpGrpcAccessLogConfig config_;
  std::shared_ptr<MockGrpcAccessLogger> logger_{new MockGrpcAccessLogger()};
  std::shared_ptr<MockGrpcAccessLoggerCache> logger_cache_{new MockGrpcAccessLoggerCache()};
  AccessLogPtr access_log_;
};

// Test HTTP log marshaling.
TEST_F(OtGrpcAccessLogTest, Marshalling) {
  InSequence s;

  //   {
  //     NiceMock<StreamInfo::MockStreamInfo> stream_info;
  //     stream_info.host_ = nullptr;
  //     stream_info.start_time_ = SystemTime(1h);
  //     stream_info.start_time_monotonic_ = MonotonicTime(1h);
  //     stream_info.last_downstream_tx_byte_sent_ = 2ms;
  //     stream_info.downstream_address_provider_->setLocalAddress(
  //         std::make_shared<Network::Address::PipeInstance>("/foo"));
  //     (*stream_info.metadata_.mutable_filter_metadata())["foo"] = ProtobufWkt::Struct();
  //     expectLog(R"EOF(
  // common_properties:
  //   downstream_remote_address:
  //     socket_address:
  //       address: "127.0.0.1"
  //       port_value: 0
  //   downstream_direct_remote_address:
  //     socket_address:
  //       address: "127.0.0.1"
  //       port_value: 0
  //   downstream_local_address:
  //     pipe:
  //       path: "/foo"
  //   start_time:
  //     seconds: 3600
  //   time_to_last_downstream_tx_byte:
  //     nanos: 2000000
  //   metadata:
  //     filter_metadata:
  //       foo: {}
  //   filter_state_objects:
  //     string_accessor:
  //       "@type": type.googleapis.com/google.protobuf.StringValue
  //       value: test_value
  //     serialized:
  //       "@type": type.googleapis.com/google.protobuf.Duration
  //       value: 10s
  // request: {}
  // response: {}
  // )EOF");
  //     access_log_->log(nullptr, nullptr, nullptr, stream_info);
  //   }

  //   {
  //     NiceMock<StreamInfo::MockStreamInfo> stream_info;
  //     stream_info.host_ = nullptr;
  //     stream_info.start_time_ = SystemTime(1h);
  //     stream_info.last_downstream_tx_byte_sent_ = std::chrono::nanoseconds(2000000);

  //     expectLog(R"EOF(
  // common_properties:
  //   downstream_remote_address:
  //     socket_address:
  //       address: "127.0.0.1"
  //       port_value: 0
  //   downstream_direct_remote_address:
  //     socket_address:
  //       address: "127.0.0.1"
  //       port_value: 0
  //   downstream_local_address:
  //     socket_address:
  //       address: "127.0.0.2"
  //       port_value: 0
  //   start_time:
  //     seconds: 3600
  //   time_to_last_downstream_tx_byte:
  //     nanos: 2000000
  // request: {}
  // response: {}
  // )EOF");
  //     access_log_->log(nullptr, nullptr, nullptr, stream_info);
  //   }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.start_time_ = SystemTime(1h);

    stream_info.last_downstream_rx_byte_received_ = 2ms;
    stream_info.first_upstream_tx_byte_sent_ = 4ms;
    stream_info.last_upstream_tx_byte_sent_ = 6ms;
    stream_info.first_upstream_rx_byte_received_ = 8ms;
    stream_info.last_upstream_rx_byte_received_ = 10ms;
    stream_info.first_downstream_tx_byte_sent_ = 12ms;
    stream_info.last_downstream_tx_byte_sent_ = 14ms;

    stream_info.setUpstreamLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("10.0.0.2"));
    stream_info.protocol_ = Http::Protocol::Http10;
    stream_info.addBytesReceived(10);
    stream_info.addBytesSent(20);
    stream_info.response_code_ = 200;
    stream_info.response_code_details_ = "via_upstream";
    absl::string_view route_name_view("route-name-test");
    stream_info.setRouteName(route_name_view);
    ON_CALL(stream_info, hasResponseFlag(StreamInfo::ResponseFlag::FaultInjected))
        .WillByDefault(Return(true));

    Http::TestRequestHeaderMapImpl request_headers{
        {":scheme", "scheme_value"},
        {":authority", "authority_value"},
        {":path", "path_value"},
        {":method", "POST"},
        {"user-agent", "user-agent_value"},
        {"referer", "referer_value"},
        {"x-forwarded-for", "x-forwarded-for_value"},
        {"x-request-id", "x-request-id_value"},
        {"x-envoy-original-path", "x-envoy-original-path_value"},
        {"x-envoy-downstream-service-cluster", "downstream_service_id"},
    };
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};

    expectLog(R"EOF(
      body:
        kvlist_value:
          values:
            - key: "iitamark"
              value:
                string_value: "iitamark-value"
    )EOF");

    //     expectLog(R"EOF(
    // common_properties:
    //   downstream_remote_address:
    //     socket_address:
    //       address: "127.0.0.1"
    //       port_value: 0
    //   downstream_direct_remote_address:
    //     socket_address:
    //       address: "127.0.0.1"
    //       port_value: 0
    //   downstream_local_address:
    //     socket_address:
    //       address: "127.0.0.2"
    //       port_value: 0
    //   start_time:
    //     seconds: 3600
    //   time_to_last_rx_byte:
    //     nanos: 2000000
    //   time_to_first_upstream_tx_byte:
    //     nanos: 4000000
    //   time_to_last_upstream_tx_byte:
    //     nanos:  6000000
    //   time_to_first_upstream_rx_byte:
    //     nanos: 8000000
    //   time_to_last_upstream_rx_byte:
    //     nanos: 10000000
    //   time_to_first_downstream_tx_byte:
    //     nanos: 12000000
    //   time_to_last_downstream_tx_byte:
    //     nanos: 14000000
    //   upstream_remote_address:
    //     socket_address:
    //       address: "10.0.0.1"
    //       port_value: 443
    //   upstream_local_address:
    //     socket_address:
    //       address: "10.0.0.2"
    //       port_value: 0
    //   upstream_cluster: "fake_cluster"
    //   response_flags:
    //     fault_injected: true
    //   route_name: "route-name-test"
    // protocol_version: HTTP10
    // request:
    //   scheme: "scheme_value"
    //   authority: "authority_value"
    //   path: "path_value"
    //   user_agent: "user-agent_value"
    //   referer: "referer_value"
    //   forwarded_for: "x-forwarded-for_value"
    //   request_id: "x-request-id_value"
    //   original_path: "x-envoy-original-path_value"
    //   request_headers_bytes: 230
    //   request_body_bytes: 10
    //   request_method: "POST"
    // response:
    //   response_code:
    //     value: 200
    //   response_headers_bytes: 10
    //   response_body_bytes: 20
    //   response_code_details: "via_upstream"
    // )EOF");
    access_log_->log(&request_headers, &response_headers, nullptr, stream_info);
  }

  //   {
  //     NiceMock<StreamInfo::MockStreamInfo> stream_info;
  //     stream_info.host_ = nullptr;
  //     stream_info.start_time_ = SystemTime(1h);
  //     stream_info.upstream_transport_failure_reason_ = "TLS error";

  //     Http::TestRequestHeaderMapImpl request_headers{
  //         {":method", "WHACKADOO"},
  //     };

  //     expectLog(R"EOF(
  // common_properties:
  //   downstream_remote_address:
  //     socket_address:
  //       address: "127.0.0.1"
  //       port_value: 0
  //   downstream_direct_remote_address:
  //     socket_address:
  //       address: "127.0.0.1"
  //       port_value: 0
  //   downstream_local_address:
  //     socket_address:
  //       address: "127.0.0.2"
  //       port_value: 0
  //   start_time:
  //     seconds: 3600
  //   upstream_transport_failure_reason: "TLS error"
  // request:
  //   request_method: "METHOD_UNSPECIFIED"
  //   request_headers_bytes: 16
  // response: {}
  // )EOF");
  //     access_log_->log(&request_headers, nullptr, nullptr, stream_info);
  //   }

  //   {
  //     NiceMock<StreamInfo::MockStreamInfo> stream_info;
  //     stream_info.host_ = nullptr;
  //     stream_info.start_time_ = SystemTime(1h);

  //     auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  //     const std::vector<std::string> peerSans{"peerSan1", "peerSan2"};
  //     ON_CALL(*connection_info, uriSanPeerCertificate()).WillByDefault(Return(peerSans));
  //     const std::vector<std::string> localSans{"localSan1", "localSan2"};
  //     ON_CALL(*connection_info, uriSanLocalCertificate()).WillByDefault(Return(localSans));
  //     const std::string peerSubject = "peerSubject";
  //     ON_CALL(*connection_info, subjectPeerCertificate()).WillByDefault(ReturnRef(peerSubject));
  //     const std::string localSubject = "localSubject";
  //     ON_CALL(*connection_info,
  //     subjectLocalCertificate()).WillByDefault(ReturnRef(localSubject)); const std::string
  //     sessionId =
  //         "D62A523A65695219D46FE1FFE285A4C371425ACE421B110B5B8D11D3EB4D5F0B";
  //     ON_CALL(*connection_info, sessionId()).WillByDefault(ReturnRef(sessionId));
  //     const std::string tlsVersion = "TLSv1.3";
  //     ON_CALL(*connection_info, tlsVersion()).WillByDefault(ReturnRef(tlsVersion));
  //     ON_CALL(*connection_info, ciphersuiteId()).WillByDefault(Return(0x2CC0));
  //     stream_info.setDownstreamSslConnection(connection_info);
  //     stream_info.requested_server_name_ = "sni";

  //     Http::TestRequestHeaderMapImpl request_headers{
  //         {":method", "WHACKADOO"},
  //     };

  //     expectLog(R"EOF(
  // common_properties:
  //   downstream_remote_address:
  //     socket_address:
  //       address: "127.0.0.1"
  //       port_value: 0
  //   downstream_direct_remote_address:
  //     socket_address:
  //       address: "127.0.0.1"
  //       port_value: 0
  //   downstream_local_address:
  //     socket_address:
  //       address: "127.0.0.2"
  //       port_value: 0
  //   start_time:
  //     seconds: 3600
  //   tls_properties:
  //     tls_version: TLSv1_3
  //     tls_cipher_suite: 0x2cc0
  //     tls_sni_hostname: sni
  //     local_certificate_properties:
  //       subject_alt_name:
  //       - uri: localSan1
  //       - uri: localSan2
  //       subject: localSubject
  //     peer_certificate_properties:
  //       subject_alt_name:
  //       - uri: peerSan1
  //       - uri: peerSan2
  //       subject: peerSubject
  //     tls_session_id: D62A523A65695219D46FE1FFE285A4C371425ACE421B110B5B8D11D3EB4D5F0B
  // request:
  //   request_method: "METHOD_UNSPECIFIED"
  //   request_headers_bytes: 16
  // response: {}
  // )EOF");
  //     access_log_->log(&request_headers, nullptr, nullptr, stream_info);
  //   }

  //   // TLSv1.2
  //   {
  //     NiceMock<StreamInfo::MockStreamInfo> stream_info;
  //     stream_info.host_ = nullptr;
  //     stream_info.start_time_ = SystemTime(1h);

  //     auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  //     const std::string empty;
  //     ON_CALL(*connection_info, subjectPeerCertificate()).WillByDefault(ReturnRef(empty));
  //     ON_CALL(*connection_info, subjectLocalCertificate()).WillByDefault(ReturnRef(empty));
  //     ON_CALL(*connection_info, sessionId()).WillByDefault(ReturnRef(empty));
  //     const std::string tlsVersion = "TLSv1.2";
  //     ON_CALL(*connection_info, tlsVersion()).WillByDefault(ReturnRef(tlsVersion));
  //     ON_CALL(*connection_info, ciphersuiteId()).WillByDefault(Return(0x2F));
  //     stream_info.setDownstreamSslConnection(connection_info);
  //     stream_info.requested_server_name_ = "sni";

  //     Http::TestRequestHeaderMapImpl request_headers{
  //         {":method", "WHACKADOO"},
  //     };

  //     expectLog(R"EOF(
  // common_properties:
  //   downstream_remote_address:
  //     socket_address:
  //       address: "127.0.0.1"
  //       port_value: 0
  //   downstream_direct_remote_address:
  //     socket_address:
  //       address: "127.0.0.1"
  //       port_value: 0
  //   downstream_local_address:
  //     socket_address:
  //       address: "127.0.0.2"
  //       port_value: 0
  //   start_time:
  //     seconds: 3600
  //   tls_properties:
  //     tls_version: TLSv1_2
  //     tls_cipher_suite: 0x2f
  //     tls_sni_hostname: sni
  //     local_certificate_properties: {}
  //     peer_certificate_properties: {}
  // request:
  //   request_method: "METHOD_UNSPECIFIED"
  // response: {}
  // )EOF");
  //     access_log_->log(nullptr, nullptr, nullptr, stream_info);
  //   }

  //   // TLSv1.1
  //   {
  //     NiceMock<StreamInfo::MockStreamInfo> stream_info;
  //     stream_info.host_ = nullptr;
  //     stream_info.start_time_ = SystemTime(1h);

  //     auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  //     const std::string empty;
  //     ON_CALL(*connection_info, subjectPeerCertificate()).WillByDefault(ReturnRef(empty));
  //     ON_CALL(*connection_info, subjectLocalCertificate()).WillByDefault(ReturnRef(empty));
  //     ON_CALL(*connection_info, sessionId()).WillByDefault(ReturnRef(empty));
  //     const std::string tlsVersion = "TLSv1.1";
  //     ON_CALL(*connection_info, tlsVersion()).WillByDefault(ReturnRef(tlsVersion));
  //     ON_CALL(*connection_info, ciphersuiteId()).WillByDefault(Return(0x2F));
  //     stream_info.setDownstreamSslConnection(connection_info);
  //     stream_info.requested_server_name_ = "sni";

  //     Http::TestRequestHeaderMapImpl request_headers{
  //         {":method", "WHACKADOO"},
  //     };

  //     expectLog(R"EOF(
  // common_properties:
  //   downstream_remote_address:
  //     socket_address:
  //       address: "127.0.0.1"
  //       port_value: 0
  //   downstream_direct_remote_address:
  //     socket_address:
  //       address: "127.0.0.1"
  //       port_value: 0
  //   downstream_local_address:
  //     socket_address:
  //       address: "127.0.0.2"
  //       port_value: 0
  //   start_time:
  //     seconds: 3600
  //   tls_properties:
  //     tls_version: TLSv1_1
  //     tls_cipher_suite: 0x2f
  //     tls_sni_hostname: sni
  //     local_certificate_properties: {}
  //     peer_certificate_properties: {}
  // request:
  //   request_method: "METHOD_UNSPECIFIED"
  // response: {}
  // )EOF");
  //     access_log_->log(nullptr, nullptr, nullptr, stream_info);
  //   }

  //   // TLSv1
  //   {
  //     NiceMock<StreamInfo::MockStreamInfo> stream_info;
  //     stream_info.host_ = nullptr;
  //     stream_info.start_time_ = SystemTime(1h);

  //     auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  //     const std::string empty;
  //     ON_CALL(*connection_info, subjectPeerCertificate()).WillByDefault(ReturnRef(empty));
  //     ON_CALL(*connection_info, subjectLocalCertificate()).WillByDefault(ReturnRef(empty));
  //     ON_CALL(*connection_info, sessionId()).WillByDefault(ReturnRef(empty));
  //     const std::string tlsVersion = "TLSv1";
  //     ON_CALL(*connection_info, tlsVersion()).WillByDefault(ReturnRef(tlsVersion));
  //     ON_CALL(*connection_info, ciphersuiteId()).WillByDefault(Return(0x2F));
  //     stream_info.setDownstreamSslConnection(connection_info);
  //     stream_info.requested_server_name_ = "sni";

  //     Http::TestRequestHeaderMapImpl request_headers{
  //         {":method", "WHACKADOO"},
  //     };

  //     expectLog(R"EOF(
  // common_properties:
  //   downstream_remote_address:
  //     socket_address:
  //       address: "127.0.0.1"
  //       port_value: 0
  //   downstream_direct_remote_address:
  //     socket_address:
  //       address: "127.0.0.1"
  //       port_value: 0
  //   downstream_local_address:
  //     socket_address:
  //       address: "127.0.0.2"
  //       port_value: 0
  //   start_time:
  //     seconds: 3600
  //   tls_properties:
  //     tls_version: TLSv1
  //     tls_cipher_suite: 0x2f
  //     tls_sni_hostname: sni
  //     local_certificate_properties: {}
  //     peer_certificate_properties: {}
  // request:
  //   request_method: "METHOD_UNSPECIFIED"
  // response: {}
  // )EOF");
  //     access_log_->log(nullptr, nullptr, nullptr, stream_info);
  //   }

  //   // Unknown TLS version (TLSv1.4)
  //   {
  //     NiceMock<StreamInfo::MockStreamInfo> stream_info;
  //     stream_info.host_ = nullptr;
  //     stream_info.start_time_ = SystemTime(1h);

  //     auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  //     const std::string empty;
  //     ON_CALL(*connection_info, subjectPeerCertificate()).WillByDefault(ReturnRef(empty));
  //     ON_CALL(*connection_info, subjectLocalCertificate()).WillByDefault(ReturnRef(empty));
  //     ON_CALL(*connection_info, sessionId()).WillByDefault(ReturnRef(empty));
  //     const std::string tlsVersion = "TLSv1.4";
  //     ON_CALL(*connection_info, tlsVersion()).WillByDefault(ReturnRef(tlsVersion));
  //     ON_CALL(*connection_info, ciphersuiteId()).WillByDefault(Return(0x2F));
  //     stream_info.setDownstreamSslConnection(connection_info);
  //     stream_info.requested_server_name_ = "sni";

  //     Http::TestRequestHeaderMapImpl request_headers{
  //         {":method", "WHACKADOO"},
  //     };

  //     expectLog(R"EOF(
  // common_properties:
  //   downstream_remote_address:
  //     socket_address:
  //       address: "127.0.0.1"
  //       port_value: 0
  //   downstream_direct_remote_address:
  //     socket_address:
  //       address: "127.0.0.1"
  //       port_value: 0
  //   downstream_local_address:
  //     socket_address:
  //       address: "127.0.0.2"
  //       port_value: 0
  //   start_time:
  //     seconds: 3600
  //   tls_properties:
  //     tls_version: VERSION_UNSPECIFIED
  //     tls_cipher_suite: 0x2f
  //     tls_sni_hostname: sni
  //     local_certificate_properties: {}
  //     peer_certificate_properties: {}
  // request:
  //   request_method: "METHOD_UNSPECIFIED"
  // response: {}
  // )EOF");
  //     access_log_->log(nullptr, nullptr, nullptr, stream_info);
  //   }
}

} // namespace
} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
