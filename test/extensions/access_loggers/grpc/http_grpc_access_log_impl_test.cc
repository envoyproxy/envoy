#include <memory>

#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/uint32_accessor_impl.h"
#include "source/common/stream_info/utility.h"
#include "source/extensions/access_loggers/grpc/http_grpc_access_log_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/thread_local/mocks.h"

using namespace std::chrono_literals;
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
namespace HttpGrpc {
namespace {

using envoy::data::accesslog::v3::HTTPAccessLogEntry;

class MockGrpcAccessLogger : public GrpcCommon::GrpcAccessLogger {
public:
  // GrpcAccessLogger
  MOCK_METHOD(void, log, (HTTPAccessLogEntry && entry));
  MOCK_METHOD(void, log, (envoy::data::accesslog::v3::TCPAccessLogEntry && entry));
};

class MockGrpcAccessLoggerCache : public GrpcCommon::GrpcAccessLoggerCache {
public:
  // GrpcAccessLoggerCache
  MOCK_METHOD(GrpcCommon::GrpcAccessLoggerSharedPtr, getOrCreateLogger,
              (const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
               Common::GrpcAccessLoggerType logger_type));
};

// Test for the issue described in https://github.com/envoyproxy/envoy/pull/18081
TEST(HttpGrpcAccessLog, TlsLifetimeCheck) {
  NiceMock<ThreadLocal::MockInstance> tls;
  Stats::IsolatedStoreImpl scope;
  std::shared_ptr<MockGrpcAccessLoggerCache> logger_cache{new MockGrpcAccessLoggerCache()};
  tls.defer_data_ = true;
  {
    AccessLog::MockFilter* filter{new NiceMock<AccessLog::MockFilter>()};
    envoy::extensions::access_loggers::grpc::v3::HttpGrpcAccessLogConfig config;
    config.mutable_common_config()->set_transport_api_version(
        envoy::config::core::v3::ApiVersion::V3);
    EXPECT_CALL(*logger_cache, getOrCreateLogger(_, _))
        .WillOnce([](const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig&
                         common_config,
                     Common::GrpcAccessLoggerType type) {
          // This is a part of the actual getOrCreateLogger code path and shouldn't crash.
          std::make_pair(MessageUtil::hash(common_config), type);
          return nullptr;
        });
    // Set tls callback in the HttpGrpcAccessLog constructor,
    // but it is not called yet since we have defer_data_ = true.
    const auto access_log = std::make_unique<HttpGrpcAccessLog>(AccessLog::FilterPtr{filter},
                                                                config, tls, logger_cache);
    // Intentionally make access_log die earlier in this scope to simulate the situation where the
    // creator has been deleted yet the tls callback is not called yet.
  }
  // Verify the tls callback does not crash since it captures the env with proper lifetime.
  tls.call();
}

class HttpGrpcAccessLogTest : public testing::Test {
public:
  void init() {
    ON_CALL(*filter_, evaluate(_, _)).WillByDefault(Return(true));
    config_.mutable_common_config()->set_log_name("hello_log");
    config_.mutable_common_config()->add_filter_state_objects_to_log("string_accessor");
    config_.mutable_common_config()->add_filter_state_objects_to_log("uint32_accessor");
    config_.mutable_common_config()->add_filter_state_objects_to_log("serialized");
    config_.mutable_common_config()->set_transport_api_version(
        envoy::config::core::v3::ApiVersion::V3);
    EXPECT_CALL(*logger_cache_, getOrCreateLogger(_, _))
        .WillOnce(
            [this](const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig&
                       config,
                   Common::GrpcAccessLoggerType logger_type) {
              EXPECT_EQ(config.DebugString(), config_.common_config().DebugString());
              EXPECT_EQ(Common::GrpcAccessLoggerType::HTTP, logger_type);
              return logger_;
            });
    access_log_ = std::make_unique<HttpGrpcAccessLog>(AccessLog::FilterPtr{filter_}, config_, tls_,
                                                      logger_cache_);
  }

  void expectLog(const std::string& expected_log_entry_yaml) {
    if (access_log_ == nullptr) {
      init();
    }

    HTTPAccessLogEntry expected_log_entry;
    TestUtility::loadFromYaml(expected_log_entry_yaml, expected_log_entry);
    EXPECT_CALL(*logger_, log(An<HTTPAccessLogEntry&&>()))
        .WillOnce(
            Invoke([expected_log_entry](envoy::data::accesslog::v3::HTTPAccessLogEntry&& entry) {
              entry.mutable_common_properties()->clear_duration();
              EXPECT_EQ(entry.DebugString(), expected_log_entry.DebugString());
            }));
  }

  void expectLogRequestMethod(const std::string& request_method) {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.start_time_ = SystemTime(1h);
    stream_info.upstreamInfo()->setUpstreamHost(nullptr);
    stream_info.onRequestComplete();

    Http::TestRequestHeaderMapImpl request_headers{
        {":method", request_method},
    };

    expectLog(fmt::format(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  downstream_direct_remote_address:
    socket_address:
      address: "127.0.0.3"
      port_value: 63443
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
  access_log_type: NotSet
  upstream_local_address:
    socket_address:
      address: "127.1.2.3"
      port_value: 58443
  start_time:
    seconds: 3600
request:
  request_method: {}
  request_headers_bytes: {}
response: {{}}
    )EOF",
                          request_method, request_method.length() + 7));
    access_log_->log({&request_headers}, stream_info);
  }

  Stats::IsolatedStoreImpl scope_;
  AccessLog::MockFilter* filter_{new NiceMock<AccessLog::MockFilter>()};
  NiceMock<ThreadLocal::MockInstance> tls_;
  envoy::extensions::access_loggers::grpc::v3::HttpGrpcAccessLogConfig config_;
  std::shared_ptr<MockGrpcAccessLogger> logger_{new MockGrpcAccessLogger()};
  std::shared_ptr<MockGrpcAccessLoggerCache> logger_cache_{new MockGrpcAccessLoggerCache()};
  HttpGrpcAccessLogPtr access_log_;
};

class TestSerializedFilterState : public StreamInfo::FilterState::Object {
public:
  ProtobufTypes::MessagePtr serializeAsProto() const override {
    auto any = std::make_unique<ProtobufWkt::Any>();
    ProtobufWkt::Duration value;
    value.set_seconds(10);
    any->PackFrom(value);
    return any;
  }
};

// Test HTTP log marshaling.
TEST_F(HttpGrpcAccessLogTest, Marshalling) {
  InSequence s;
  NiceMock<MockTimeSystem> time_system;

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.upstreamInfo()->setUpstreamHost(nullptr);
    stream_info.start_time_ = SystemTime(1h);
    stream_info.start_time_monotonic_ = MonotonicTime(1h);
    EXPECT_CALL(time_system, monotonicTime)
        .WillOnce(Return(MonotonicTime(std::chrono::hours(1) + std::chrono::milliseconds(2))));
    stream_info.downstream_timing_.onLastDownstreamTxByteSent(time_system);
    StreamInfo::TimingUtility timing(stream_info);
    ASSERT(timing.lastDownstreamTxByteSent().has_value());
    stream_info.downstream_connection_info_provider_->setLocalAddress(
        *Network::Address::PipeInstance::create("/foo"));
    (*stream_info.metadata_.mutable_filter_metadata())["foo"] = ProtobufWkt::Struct();
    stream_info.filter_state_->setData("string_accessor",
                                       std::make_unique<Router::StringAccessorImpl>("test_value"),
                                       StreamInfo::FilterState::StateType::ReadOnly,
                                       StreamInfo::FilterState::LifeSpan::FilterChain);
    stream_info.filter_state_->setData("uint32_accessor",
                                       std::make_unique<StreamInfo::UInt32AccessorImpl>(42),
                                       StreamInfo::FilterState::StateType::ReadOnly,
                                       StreamInfo::FilterState::LifeSpan::FilterChain);
    stream_info.filter_state_->setData("serialized", std::make_unique<TestSerializedFilterState>(),
                                       StreamInfo::FilterState::StateType::ReadOnly,
                                       StreamInfo::FilterState::LifeSpan::FilterChain);
    stream_info.onRequestComplete();

    expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  access_log_type: NotSet
  downstream_direct_remote_address:
    socket_address:
      address: "127.0.0.3"
      port_value: 63443
  downstream_local_address:
    pipe:
      path: "/foo"
  upstream_local_address:
    socket_address:
      address: "127.1.2.3"
      port_value: 58443
  start_time:
    seconds: 3600
  time_to_last_downstream_tx_byte:
    nanos: 2000000
  metadata:
    filter_metadata:
      foo: {}
  filter_state_objects:
    string_accessor:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: test_value
    uint32_accessor:
      "@type": type.googleapis.com/google.protobuf.UInt32Value
      value: 42
    serialized:
      "@type": type.googleapis.com/google.protobuf.Duration
      value: 10s
request: {}
response: {}
)EOF");
    access_log_->log({}, stream_info);
  }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.upstreamInfo()->setUpstreamHost(nullptr);
    stream_info.start_time_ = SystemTime(1h);
    EXPECT_CALL(time_system, monotonicTime)
        .WillOnce(Return(MonotonicTime(std::chrono::nanoseconds(2000000))));
    stream_info.downstream_timing_.onLastDownstreamTxByteSent(time_system);
    stream_info.onRequestComplete();

    expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  access_log_type: NotSet
  downstream_direct_remote_address:
    socket_address:
      address: "127.0.0.3"
      port_value: 63443
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
  upstream_local_address:
    socket_address:
      address: "127.1.2.3"
      port_value: 58443
  start_time:
    seconds: 3600
  time_to_last_downstream_tx_byte:
    nanos: 2000000
request: {}
response: {}
)EOF");
    access_log_->log({}, stream_info);
  }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.start_time_ = SystemTime(1h);

    MockTimeSystem time_system;
    EXPECT_CALL(time_system, monotonicTime)
        .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(2))));
    stream_info.downstream_timing_.onLastDownstreamRxByteReceived(time_system);
    stream_info.upstream_info_->upstreamTiming().first_upstream_tx_byte_sent_ =
        MonotonicTime(std::chrono::milliseconds(4));
    stream_info.upstream_info_->upstreamTiming().last_upstream_tx_byte_sent_ =
        MonotonicTime(std::chrono::milliseconds(6));
    stream_info.upstream_info_->upstreamTiming().first_upstream_rx_byte_received_ =
        MonotonicTime(std::chrono::milliseconds(8));
    stream_info.upstream_info_->upstreamTiming().last_upstream_rx_byte_received_ =
        MonotonicTime(std::chrono::milliseconds(10));
    EXPECT_CALL(time_system, monotonicTime)
        .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(12))));
    stream_info.downstream_timing_.onFirstDownstreamTxByteSent(time_system);
    EXPECT_CALL(time_system, monotonicTime)
        .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(14))));
    stream_info.downstream_timing_.onLastDownstreamTxByteSent(time_system);

    stream_info.upstream_info_->setUpstreamLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("10.0.0.2"));
    stream_info.protocol_ = Http::Protocol::Http10;
    stream_info.addBytesReceived(10);
    stream_info.addBytesSent(20);
    stream_info.setResponseCode(200);
    stream_info.response_code_details_ = "via_upstream";
    const std::string route_name("route-name-test");
    ON_CALL(stream_info, getRouteName()).WillByDefault(ReturnRef(route_name));

    ON_CALL(stream_info, hasResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected))
        .WillByDefault(Return(true));
    stream_info.onRequestComplete();

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
    };
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};

    expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  access_log_type: NotSet
  downstream_direct_remote_address:
    socket_address:
      address: "127.0.0.3"
      port_value: 63443
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
  upstream_local_address:
    socket_address:
      address: "127.1.2.3"
      port_value: 58443
  start_time:
    seconds: 3600
  time_to_last_rx_byte:
    nanos: 2000000
  time_to_first_upstream_tx_byte:
    nanos: 4000000
  time_to_last_upstream_tx_byte:
    nanos:  6000000
  time_to_first_upstream_rx_byte:
    nanos: 8000000
  time_to_last_upstream_rx_byte:
    nanos: 10000000
  time_to_first_downstream_tx_byte:
    nanos: 12000000
  time_to_last_downstream_tx_byte:
    nanos: 14000000
  upstream_remote_address:
    socket_address:
      address: "10.0.0.1"
      port_value: 443
  upstream_local_address:
    socket_address:
      address: "10.0.0.2"
      port_value: 0
  upstream_cluster: "fake_cluster"
  response_flags:
    fault_injected: true
  route_name: "route-name-test"
protocol_version: HTTP10
request:
  scheme: "scheme_value"
  authority: "authority_value"
  path: "path_value"
  user_agent: "user-agent_value"
  referer: "referer_value"
  forwarded_for: "x-forwarded-for_value"
  request_id: "x-request-id_value"
  original_path: "x-envoy-original-path_value"
  request_headers_bytes: 230
  request_body_bytes: 10
  request_method: "POST"
response:
  response_code:
    value: 200
  response_headers_bytes: 10
  response_body_bytes: 20
  response_code_details: "via_upstream"
)EOF");
    access_log_->log({&request_headers, &response_headers}, stream_info);
  }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.upstreamInfo()->setUpstreamHost(nullptr);
    stream_info.start_time_ = SystemTime(1h);
    stream_info.upstream_info_->setUpstreamTransportFailureReason("TLS error");
    stream_info.onRequestComplete();

    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "WHACKADOO"},
    };

    expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  access_log_type: NotSet
  downstream_direct_remote_address:
    socket_address:
      address: "127.0.0.3"
      port_value: 63443
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
  upstream_local_address:
    socket_address:
      address: "127.1.2.3"
      port_value: 58443
  start_time:
    seconds: 3600
  upstream_transport_failure_reason: "TLS error"
request:
  request_method: "METHOD_UNSPECIFIED"
  request_headers_bytes: 16
response: {}
)EOF");
    access_log_->log({&request_headers}, stream_info);
  }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.upstreamInfo()->setUpstreamHost(nullptr);
    stream_info.start_time_ = SystemTime(1h);

    auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
    const std::vector<std::string> peerSans{"peerSan1", "peerSan2"};
    ON_CALL(*connection_info, uriSanPeerCertificate()).WillByDefault(Return(peerSans));
    const std::vector<std::string> localSans{"localSan1", "localSan2"};
    ON_CALL(*connection_info, uriSanLocalCertificate()).WillByDefault(Return(localSans));
    const std::string peerSubject = "peerSubject";
    ON_CALL(*connection_info, subjectPeerCertificate()).WillByDefault(ReturnRef(peerSubject));
    const std::string peerIssuer = "peerIssuer";
    ON_CALL(*connection_info, issuerPeerCertificate()).WillByDefault(ReturnRef(peerIssuer));
    const std::string localSubject = "localSubject";
    ON_CALL(*connection_info, subjectLocalCertificate()).WillByDefault(ReturnRef(localSubject));
    const std::string sessionId =
        "D62A523A65695219D46FE1FFE285A4C371425ACE421B110B5B8D11D3EB4D5F0B";
    ON_CALL(*connection_info, sessionId()).WillByDefault(ReturnRef(sessionId));
    const std::string tlsVersion = "TLSv1.3";
    ON_CALL(*connection_info, tlsVersion()).WillByDefault(ReturnRef(tlsVersion));
    ON_CALL(*connection_info, ciphersuiteId()).WillByDefault(Return(0x2CC0));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    stream_info.downstream_connection_info_provider_->setRequestedServerName("sni");
    stream_info.onRequestComplete();

    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "WHACKADOO"},
    };

    expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  access_log_type: NotSet
  downstream_direct_remote_address:
    socket_address:
      address: "127.0.0.3"
      port_value: 63443
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
  upstream_local_address:
    socket_address:
      address: "127.1.2.3"
      port_value: 58443
  start_time:
    seconds: 3600
  tls_properties:
    tls_version: TLSv1_3
    tls_cipher_suite: 0x2cc0
    tls_sni_hostname: sni
    local_certificate_properties:
      subject_alt_name:
      - uri: localSan1
      - uri: localSan2
      subject: localSubject
    peer_certificate_properties:
      subject_alt_name:
      - uri: peerSan1
      - uri: peerSan2
      subject: peerSubject
      issuer: peerIssuer
    tls_session_id: D62A523A65695219D46FE1FFE285A4C371425ACE421B110B5B8D11D3EB4D5F0B
request:
  request_method: "METHOD_UNSPECIFIED"
  request_headers_bytes: 16
response: {}
)EOF");
    access_log_->log({&request_headers}, stream_info);
  }

  // TLSv1.2
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.upstreamInfo()->setUpstreamHost(nullptr);
    stream_info.start_time_ = SystemTime(1h);

    auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
    const std::string empty;
    ON_CALL(*connection_info, subjectPeerCertificate()).WillByDefault(ReturnRef(empty));
    ON_CALL(*connection_info, issuerPeerCertificate()).WillByDefault(ReturnRef(empty));
    ON_CALL(*connection_info, subjectLocalCertificate()).WillByDefault(ReturnRef(empty));
    ON_CALL(*connection_info, sessionId()).WillByDefault(ReturnRef(empty));
    const std::string tlsVersion = "TLSv1.2";
    ON_CALL(*connection_info, tlsVersion()).WillByDefault(ReturnRef(tlsVersion));
    ON_CALL(*connection_info, ciphersuiteId()).WillByDefault(Return(0x2F));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    stream_info.downstream_connection_info_provider_->setRequestedServerName("sni");
    stream_info.onRequestComplete();

    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "WHACKADOO"},
    };

    expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  access_log_type: NotSet
  downstream_direct_remote_address:
    socket_address:
      address: "127.0.0.3"
      port_value: 63443
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
  upstream_local_address:
    socket_address:
      address: "127.1.2.3"
      port_value: 58443
  start_time:
    seconds: 3600
  tls_properties:
    tls_version: TLSv1_2
    tls_cipher_suite: 0x2f
    tls_sni_hostname: sni
    local_certificate_properties: {}
    peer_certificate_properties: {}
request:
  request_method: "METHOD_UNSPECIFIED"
response: {}
)EOF");
    access_log_->log({}, stream_info);
  }

  // TLSv1.1
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.upstreamInfo()->setUpstreamHost(nullptr);
    stream_info.start_time_ = SystemTime(1h);

    auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
    const std::string empty;
    ON_CALL(*connection_info, subjectPeerCertificate()).WillByDefault(ReturnRef(empty));
    ON_CALL(*connection_info, issuerPeerCertificate()).WillByDefault(ReturnRef(empty));
    ON_CALL(*connection_info, subjectLocalCertificate()).WillByDefault(ReturnRef(empty));
    ON_CALL(*connection_info, sessionId()).WillByDefault(ReturnRef(empty));
    const std::string tlsVersion = "TLSv1.1";
    ON_CALL(*connection_info, tlsVersion()).WillByDefault(ReturnRef(tlsVersion));
    ON_CALL(*connection_info, ciphersuiteId()).WillByDefault(Return(0x2F));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    stream_info.downstream_connection_info_provider_->setRequestedServerName("sni");
    stream_info.onRequestComplete();

    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "WHACKADOO"},
    };

    expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  access_log_type: NotSet
  downstream_direct_remote_address:
    socket_address:
      address: "127.0.0.3"
      port_value: 63443
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
  upstream_local_address:
    socket_address:
      address: "127.1.2.3"
      port_value: 58443
  start_time:
    seconds: 3600
  tls_properties:
    tls_version: TLSv1_1
    tls_cipher_suite: 0x2f
    tls_sni_hostname: sni
    local_certificate_properties: {}
    peer_certificate_properties: {}
request:
  request_method: "METHOD_UNSPECIFIED"
response: {}
)EOF");
    access_log_->log({}, stream_info);
  }

  // TLSv1
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.upstreamInfo()->setUpstreamHost(nullptr);
    stream_info.start_time_ = SystemTime(1h);

    auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
    const std::string empty;
    ON_CALL(*connection_info, subjectPeerCertificate()).WillByDefault(ReturnRef(empty));
    ON_CALL(*connection_info, issuerPeerCertificate()).WillByDefault(ReturnRef(empty));
    ON_CALL(*connection_info, subjectLocalCertificate()).WillByDefault(ReturnRef(empty));
    ON_CALL(*connection_info, sessionId()).WillByDefault(ReturnRef(empty));
    const std::string tlsVersion = "TLSv1";
    ON_CALL(*connection_info, tlsVersion()).WillByDefault(ReturnRef(tlsVersion));
    ON_CALL(*connection_info, ciphersuiteId()).WillByDefault(Return(0x2F));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    stream_info.downstream_connection_info_provider_->setRequestedServerName("sni");
    stream_info.onRequestComplete();

    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "WHACKADOO"},
    };

    expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  access_log_type: NotSet
  downstream_direct_remote_address:
    socket_address:
      address: "127.0.0.3"
      port_value: 63443
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
  upstream_local_address:
    socket_address:
      address: "127.1.2.3"
      port_value: 58443
  start_time:
    seconds: 3600
  tls_properties:
    tls_version: TLSv1
    tls_cipher_suite: 0x2f
    tls_sni_hostname: sni
    local_certificate_properties: {}
    peer_certificate_properties: {}
request:
  request_method: "METHOD_UNSPECIFIED"
response: {}
)EOF");
    access_log_->log({}, stream_info);
  }

  // Unknown TLS version (TLSv1.4)
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.upstreamInfo()->setUpstreamHost(nullptr);
    stream_info.start_time_ = SystemTime(1h);

    auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
    const std::string empty;
    ON_CALL(*connection_info, subjectPeerCertificate()).WillByDefault(ReturnRef(empty));
    ON_CALL(*connection_info, issuerPeerCertificate()).WillByDefault(ReturnRef(empty));
    ON_CALL(*connection_info, subjectLocalCertificate()).WillByDefault(ReturnRef(empty));
    ON_CALL(*connection_info, sessionId()).WillByDefault(ReturnRef(empty));
    const std::string tlsVersion = "TLSv1.4";
    ON_CALL(*connection_info, tlsVersion()).WillByDefault(ReturnRef(tlsVersion));
    ON_CALL(*connection_info, ciphersuiteId()).WillByDefault(Return(0x2F));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    stream_info.downstream_connection_info_provider_->setRequestedServerName("sni");
    stream_info.onRequestComplete();

    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "WHACKADOO"},
    };

    expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  access_log_type: NotSet
  downstream_direct_remote_address:
    socket_address:
      address: "127.0.0.3"
      port_value: 63443
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
  upstream_local_address:
    socket_address:
      address: "127.1.2.3"
      port_value: 58443
  start_time:
    seconds: 3600
  tls_properties:
    tls_version: VERSION_UNSPECIFIED
    tls_cipher_suite: 0x2f
    tls_sni_hostname: sni
    local_certificate_properties: {}
    peer_certificate_properties: {}
request:
  request_method: "METHOD_UNSPECIFIED"
response: {}
)EOF");
    access_log_->log({}, stream_info);
  }

  // Intermediate log entry.
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.upstreamInfo()->setUpstreamHost(nullptr);
    stream_info.start_time_ = SystemTime(1h);
    stream_info.start_time_monotonic_ = MonotonicTime(1h);
    EXPECT_CALL(time_system, monotonicTime)
        .WillOnce(Return(MonotonicTime(std::chrono::hours(1) + std::chrono::milliseconds(2))));
    stream_info.downstream_timing_.onLastDownstreamTxByteSent(time_system);
    StreamInfo::TimingUtility timing(stream_info);
    ASSERT(timing.lastDownstreamTxByteSent().has_value());
    stream_info.downstream_connection_info_provider_->setLocalAddress(
        *Network::Address::PipeInstance::create("/foo"));
    (*stream_info.metadata_.mutable_filter_metadata())["foo"] = ProtobufWkt::Struct();
    stream_info.filter_state_->setData("string_accessor",
                                       std::make_unique<Router::StringAccessorImpl>("test_value"),
                                       StreamInfo::FilterState::StateType::ReadOnly,
                                       StreamInfo::FilterState::LifeSpan::FilterChain);
    stream_info.filter_state_->setData("uint32_accessor",
                                       std::make_unique<StreamInfo::UInt32AccessorImpl>(42),
                                       StreamInfo::FilterState::StateType::ReadOnly,
                                       StreamInfo::FilterState::LifeSpan::FilterChain);
    stream_info.filter_state_->setData("serialized", std::make_unique<TestSerializedFilterState>(),
                                       StreamInfo::FilterState::StateType::ReadOnly,
                                       StreamInfo::FilterState::LifeSpan::FilterChain);

    expectLog(R"EOF(
common_properties:
  intermediate_log_entry: true
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  access_log_type: NotSet
  downstream_direct_remote_address:
    socket_address:
      address: "127.0.0.3"
      port_value: 63443
  downstream_local_address:
    pipe:
      path: "/foo"
  upstream_local_address:
    socket_address:
      address: "127.1.2.3"
      port_value: 58443
  start_time:
    seconds: 3600
  time_to_last_downstream_tx_byte:
    nanos: 2000000
  metadata:
    filter_metadata:
      foo: {}
  filter_state_objects:
    string_accessor:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: test_value
    uint32_accessor:
      "@type": type.googleapis.com/google.protobuf.UInt32Value
      value: 42
    serialized:
      "@type": type.googleapis.com/google.protobuf.Duration
      value: 10s
request: {}
response: {}
)EOF");
    access_log_->log({}, stream_info);
  }
}

// Test HTTP log marshaling with additional headers.
TEST_F(HttpGrpcAccessLogTest, MarshallingAdditionalHeaders) {
  InSequence s;

  config_.add_additional_request_headers_to_log("X-Custom-Request");
  config_.add_additional_request_headers_to_log("X-Custom-Empty");
  config_.add_additional_request_headers_to_log("X-Envoy-Max-Retries");
  config_.add_additional_request_headers_to_log("X-Envoy-Force-Trace");

  config_.add_additional_response_headers_to_log("X-Custom-Response");
  config_.add_additional_response_headers_to_log("X-Custom-Empty");
  config_.add_additional_response_headers_to_log("X-Envoy-Immediate-Health-Check-Fail");
  config_.add_additional_response_headers_to_log("X-Envoy-Upstream-Service-Time");

  config_.add_additional_response_trailers_to_log("X-Logged-Trailer");
  config_.add_additional_response_trailers_to_log("X-Missing-Trailer");
  config_.add_additional_response_trailers_to_log("X-Empty-Trailer");

  init();

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.upstreamInfo()->setUpstreamHost(nullptr);
    stream_info.start_time_ = SystemTime(1h);
    stream_info.onRequestComplete();

    Http::TestRequestHeaderMapImpl request_headers{
        {":scheme", "scheme_value"},
        {":authority", "authority_value"},
        {":path", "path_value"},
        {":method", "POST"},
        {"x-envoy-max-retries", "3"}, // test inline header not otherwise logged
        {"x-custom-request", "custom_value"},
        {"x-custom-request", "custome_value_second"},
        {"x-custom-empty", ""},
    };
    Http::TestResponseHeaderMapImpl response_headers{
        {":status", "200"},
        {"x-envoy-immediate-health-check-fail", "true"}, // test inline header not otherwise logged
        {"x-custom-response", "custom_value"},
        {"x-custom-response", "custome_response_value"},
        {"x-custom-empty", ""},
    };

    Http::TestResponseTrailerMapImpl response_trailers{
        {"x-logged-trailer", "value"},
        {"x-logged-trailer", "response_trailer_value"},
        {"x-empty-trailer", ""},
        {"x-unlogged-trailer", "2"},
    };

    expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  access_log_type: NotSet
  downstream_direct_remote_address:
    socket_address:
      address: "127.0.0.3"
      port_value: 63443
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
  upstream_local_address:
    socket_address:
      address: "127.1.2.3"
      port_value: 58443
  start_time:
    seconds: 3600
request:
  scheme: "scheme_value"
  authority: "authority_value"
  path: "path_value"
  request_method: "POST"
  request_headers_bytes: 168
  request_headers:
    "x-custom-request": "custom_value,custome_value_second"
    "x-custom-empty": ""
    "x-envoy-max-retries": "3"
response:
  response_headers_bytes: 131
  response_headers:
    "x-custom-response": "custom_value,custome_response_value"
    "x-custom-empty": ""
    "x-envoy-immediate-health-check-fail": "true"
  response_trailers:
    "x-logged-trailer": "value,response_trailer_value"
    "x-empty-trailer": ""
)EOF");
    access_log_->log({&request_headers, &response_headers, &response_trailers}, stream_info);
  }
}

// Test sanitizing non-UTF-8 header values.
TEST_F(HttpGrpcAccessLogTest, SanitizeUTF8) {
  InSequence s;

  config_.add_additional_request_headers_to_log("X-Request");
  config_.add_additional_response_headers_to_log("X-Response");
  config_.add_additional_response_trailers_to_log("X-Trailer");

  init();

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.upstreamInfo()->setUpstreamHost(nullptr);
    stream_info.start_time_ = SystemTime(1h);
    std::string non_utf8("prefix");
    non_utf8.append(1, char(0xc3));
    non_utf8.append(1, char(0xc7));
    non_utf8.append("suffix");

    Http::TestRequestHeaderMapImpl request_headers{
        {":scheme", "scheme_value"},
        {":authority", "authority_value"},
        {":path", non_utf8},
        {":method", "POST"},
        {"x-envoy-max-retries", "3"}, // test inline header not
                                      // otherwise logged
        {"x-request", non_utf8},
        {"x-request", non_utf8},
    };
    Http::TestResponseHeaderMapImpl response_headers{
        {":status", "200"},
        {"x-envoy-immediate-health-check-fail", "true"}, // test inline header not otherwise logged
        {"x-response", non_utf8},
        {"x-response", non_utf8},
    };

    Http::TestResponseTrailerMapImpl response_trailers{
        {"x-trailer", non_utf8},
        {"x-trailer", non_utf8},
    };

    stream_info.onRequestComplete();
    expectLog(fmt::format(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  access_log_type: NotSet
  downstream_direct_remote_address:
    socket_address:
      address: "127.0.0.3"
      port_value: 63443
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
  upstream_local_address:
    socket_address:
      address: "127.1.2.3"
      port_value: 58443
  start_time:
    seconds: 3600
request:
  scheme: "scheme_value"
  authority: "authority_value"
  path: "{0}"
  request_method: "POST"
  request_headers_bytes: 140
  request_headers:
    "x-request": "{0},{0}"
response:
  response_headers_bytes: 97
  response_headers:
    "x-response": "{0},{0}"
  response_trailers:
    "x-trailer": "{0},{0}"
)EOF",
                          "prefix!!suffix"));
    access_log_->log({&request_headers, &response_headers, &response_trailers}, stream_info);
  }
}

TEST_F(HttpGrpcAccessLogTest, LogWithRequestMethod) {
  InSequence s;
  expectLogRequestMethod("GET");
  expectLogRequestMethod("HEAD");
  expectLogRequestMethod("POST");
  expectLogRequestMethod("PUT");
  expectLogRequestMethod("DELETE");
  expectLogRequestMethod("CONNECT");
  expectLogRequestMethod("OPTIONS");
  expectLogRequestMethod("TRACE");
  expectLogRequestMethod("PATCH");
}

TEST_F(HttpGrpcAccessLogTest, CustomTagTestLiteral) {
  envoy::type::tracing::v3::CustomTag tag;
  const auto tag_yaml = R"EOF(
tag: ltag
literal:
  value: lvalue
  )EOF";
  TestUtility::loadFromYaml(tag_yaml, tag);
  *config_.mutable_common_config()->add_custom_tags() = tag;

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.start_time_ = SystemTime(1h);
  stream_info.onRequestComplete();

  expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  access_log_type: NotSet
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
  downstream_direct_remote_address:
    socket_address:
      address: "127.0.0.3"
      port_value: 63443
  upstream_remote_address:
    socket_address:
      address: "10.0.0.1"
      port_value: 443
  upstream_local_address:
    socket_address:
      address: "127.1.2.3"
      port_value: 58443
  upstream_cluster: "fake_cluster"
  start_time:
    seconds: 3600
  custom_tags:
    ltag: lvalue
request: {}
response: {}
)EOF");
  access_log_->log({}, stream_info);
}

TEST_F(HttpGrpcAccessLogTest, CustomTagTestMetadata) {
  envoy::type::tracing::v3::CustomTag tag;
  const auto tag_yaml = R"EOF(
tag: mtag
metadata:
  kind: { host: {} }
  metadata_key:
    key: foo
    path:
    - key: bar
  )EOF";
  TestUtility::loadFromYaml(tag_yaml, tag);
  *config_.mutable_common_config()->add_custom_tags() = tag;

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.start_time_ = SystemTime(1h);
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  auto metadata = std::make_shared<envoy::config::core::v3::Metadata>();
  metadata->mutable_filter_metadata()->insert(Protobuf::MapPair<std::string, ProtobufWkt::Struct>(
      "foo", MessageUtil::keyValueStruct("bar", "baz")));
  ON_CALL(*host, metadata()).WillByDefault(Return(metadata));
  stream_info.upstreamInfo()->setUpstreamHost(host);
  stream_info.onRequestComplete();

  expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  access_log_type: NotSet
  upstream_remote_address:
    socket_address:
      address: "10.0.0.1"
      port_value: 443
  upstream_local_address:
    socket_address:
      address: "127.1.2.3"
      port_value: 58443
  upstream_cluster: fake_cluster
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
  downstream_direct_remote_address:
    socket_address:
      address: "127.0.0.3"
      port_value: 63443
  start_time:
    seconds: 3600
  custom_tags:
    mtag: baz
request: {}
response: {}
)EOF");
  access_log_->log({}, stream_info);
}

TEST_F(HttpGrpcAccessLogTest, CustomTagTestMetadataDefaultValue) {
  envoy::type::tracing::v3::CustomTag tag;
  const auto tag_yaml = R"EOF(
tag: mtag
metadata:
  kind: { host: {} }
  metadata_key:
    key: foo
    path:
    - key: baz
  default_value: piyo
  )EOF";
  TestUtility::loadFromYaml(tag_yaml, tag);
  *config_.mutable_common_config()->add_custom_tags() = tag;

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.start_time_ = SystemTime(1h);
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  stream_info.upstreamInfo()->setUpstreamHost(host);
  stream_info.onRequestComplete();

  expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  access_log_type: NotSet
  upstream_remote_address:
    socket_address:
      address: "10.0.0.1"
      port_value: 443
  upstream_local_address:
    socket_address:
      address: "127.1.2.3"
      port_value: 58443
  upstream_cluster: fake_cluster
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
  downstream_direct_remote_address:
    socket_address:
      address: "127.0.0.3"
      port_value: 63443
  start_time:
    seconds: 3600
  custom_tags:
    mtag: piyo
request: {}
response: {}
)EOF");
  access_log_->log({}, stream_info);
}

} // namespace
} // namespace HttpGrpc
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
