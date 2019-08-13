#include <memory>

#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/network/address_impl.h"

#include "extensions/access_loggers/http_grpc/grpc_access_log_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/thread_local/mocks.h"

using namespace std::chrono_literals;
using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace HttpGrpc {
namespace {

constexpr std::chrono::milliseconds FlushInterval(10);

class GrpcAccessLoggerImplTest : public testing::Test {
public:
  using MockAccessLogStream = Grpc::MockAsyncStream;
  using AccessLogCallbacks =
      Grpc::AsyncStreamCallbacks<envoy::service::accesslog::v2::StreamAccessLogsResponse>;

  void initLogger(std::chrono::milliseconds buffer_flush_interval_msec, size_t buffer_size_bytes) {
    timer_ = new Event::MockTimer(&dispatcher_);
    EXPECT_CALL(*timer_, enableTimer(buffer_flush_interval_msec));
    logger_ = std::make_unique<GrpcAccessLoggerImpl>(Grpc::RawAsyncClientPtr{async_client_},
                                                     log_name_, buffer_flush_interval_msec,
                                                     buffer_size_bytes, dispatcher_, local_info_);
  }

  void expectStreamStart(MockAccessLogStream& stream, AccessLogCallbacks** callbacks_to_set) {
    EXPECT_CALL(*async_client_, startRaw(_, _, _))
        .WillOnce(Invoke([&stream, callbacks_to_set](absl::string_view, absl::string_view,
                                                     Grpc::RawAsyncStreamCallbacks& callbacks) {
          *callbacks_to_set = dynamic_cast<AccessLogCallbacks*>(&callbacks);
          return &stream;
        }));
  }

  void expectStreamMessage(MockAccessLogStream& stream, const std::string& expected_message_yaml) {
    envoy::service::accesslog::v2::StreamAccessLogsMessage expected_message;
    TestUtility::loadFromYaml(expected_message_yaml, expected_message);
    EXPECT_CALL(stream, sendMessageRaw_(_, false))
        .WillOnce(Invoke([expected_message](Buffer::InstancePtr& request, bool) {
          envoy::service::accesslog::v2::StreamAccessLogsMessage message;
          Buffer::ZeroCopyInputStreamImpl request_stream(std::move(request));
          EXPECT_TRUE(message.ParseFromZeroCopyStream(&request_stream));
          EXPECT_EQ(message.DebugString(), expected_message.DebugString());
        }));
  }

  std::string log_name_ = "test_log_name";
  LocalInfo::MockLocalInfo local_info_;
  Event::MockTimer* timer_ = nullptr;
  Event::MockDispatcher dispatcher_;
  Grpc::MockAsyncClient* async_client_{new Grpc::MockAsyncClient};
  std::unique_ptr<GrpcAccessLoggerImpl> logger_;
};

// Test basic stream logging flow.
TEST_F(GrpcAccessLoggerImplTest, BasicFlow) {
  InSequence s;
  initLogger(FlushInterval, 0);

  // Start a stream for the first log.
  MockAccessLogStream stream;
  AccessLogCallbacks* callbacks;
  expectStreamStart(stream, &callbacks);
  EXPECT_CALL(local_info_, node());
  expectStreamMessage(stream, R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
  log_name: test_log_name
http_logs:
  log_entry:
    request:
      path: /test/path1
)EOF");
  envoy::data::accesslog::v2::HTTPAccessLogEntry entry;
  entry.mutable_request()->set_path("/test/path1");
  logger_->log(envoy::data::accesslog::v2::HTTPAccessLogEntry(entry));

  expectStreamMessage(stream, R"EOF(
http_logs:
  log_entry:
    request:
      path: /test/path2
)EOF");
  entry.mutable_request()->set_path("/test/path2");
  logger_->log(envoy::data::accesslog::v2::HTTPAccessLogEntry(entry));

  // Verify that sending an empty response message doesn't do anything bad.
  callbacks->onReceiveMessage(
      std::make_unique<envoy::service::accesslog::v2::StreamAccessLogsResponse>());

  // Close the stream and make sure we make a new one.
  callbacks->onRemoteClose(Grpc::Status::Internal, "bad");
  expectStreamStart(stream, &callbacks);
  EXPECT_CALL(local_info_, node());
  expectStreamMessage(stream, R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
  log_name: test_log_name
http_logs:
  log_entry:
    request:
      path: /test/path3
)EOF");
  entry.mutable_request()->set_path("/test/path3");
  logger_->log(envoy::data::accesslog::v2::HTTPAccessLogEntry(entry));
}

// Test that stream failure is handled correctly.
TEST_F(GrpcAccessLoggerImplTest, StreamFailure) {
  InSequence s;
  initLogger(FlushInterval, 0);

  EXPECT_CALL(*async_client_, startRaw(_, _, _))
      .WillOnce(Invoke(
          [](absl::string_view, absl::string_view, Grpc::RawAsyncStreamCallbacks& callbacks) {
            callbacks.onRemoteClose(Grpc::Status::Internal, "bad");
            return nullptr;
          }));
  EXPECT_CALL(local_info_, node());
  envoy::data::accesslog::v2::HTTPAccessLogEntry entry;
  logger_->log(envoy::data::accesslog::v2::HTTPAccessLogEntry(entry));
}

// Test that log entries are batched.
TEST_F(GrpcAccessLoggerImplTest, Batching) {
  InSequence s;
  initLogger(FlushInterval, 100);

  MockAccessLogStream stream;
  AccessLogCallbacks* callbacks;
  expectStreamStart(stream, &callbacks);
  EXPECT_CALL(local_info_, node());
  const std::string path1(30, '1');
  const std::string path2(30, '2');
  const std::string path3(80, '3');
  expectStreamMessage(stream, fmt::format(R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
  log_name: test_log_name
http_logs:
  log_entry:
  - request:
      path: "{}"
  - request:
      path: "{}"
  - request:
      path: "{}"
)EOF",
                                          path1, path2, path3));
  envoy::data::accesslog::v2::HTTPAccessLogEntry entry;
  entry.mutable_request()->set_path(path1);
  logger_->log(envoy::data::accesslog::v2::HTTPAccessLogEntry(entry));
  entry.mutable_request()->set_path(path2);
  logger_->log(envoy::data::accesslog::v2::HTTPAccessLogEntry(entry));
  entry.mutable_request()->set_path(path3);
  logger_->log(envoy::data::accesslog::v2::HTTPAccessLogEntry(entry));

  const std::string path4(120, '4');
  expectStreamMessage(stream, fmt::format(R"EOF(
http_logs:
  log_entry:
    request:
      path: "{}"
)EOF",
                                          path4));
  entry.mutable_request()->set_path(path4);
  logger_->log(envoy::data::accesslog::v2::HTTPAccessLogEntry(entry));
}

// Test that log entries are flushed periodically.
TEST_F(GrpcAccessLoggerImplTest, Flushing) {
  InSequence s;
  initLogger(FlushInterval, 100);

  // Nothing to do yet.
  EXPECT_CALL(*timer_, enableTimer(FlushInterval));
  timer_->invokeCallback();

  envoy::data::accesslog::v2::HTTPAccessLogEntry entry;
  // Not enough data yet to trigger flush on batch size.
  entry.mutable_request()->set_path("/test/path1");
  logger_->log(envoy::data::accesslog::v2::HTTPAccessLogEntry(entry));

  MockAccessLogStream stream;
  AccessLogCallbacks* callbacks;
  expectStreamStart(stream, &callbacks);
  EXPECT_CALL(local_info_, node());
  expectStreamMessage(stream, fmt::format(R"EOF(
  identifier:
    node:
      id: node_name
      cluster: cluster_name
      locality:
        zone: zone_name
    log_name: test_log_name
  http_logs:
    log_entry:
    - request:
        path: /test/path1
  )EOF"));
  EXPECT_CALL(*timer_, enableTimer(FlushInterval));
  timer_->invokeCallback();

  // Flush on empty message does nothing.
  EXPECT_CALL(*timer_, enableTimer(FlushInterval));
  timer_->invokeCallback();
}

class GrpcAccessLoggerCacheImplTest : public testing::Test {
public:
  GrpcAccessLoggerCacheImplTest() {
    logger_cache_ = std::make_unique<GrpcAccessLoggerCacheImpl>(async_client_manager_, scope_, tls_,
                                                                local_info_);
  }

  void expectClientCreation() {
    factory_ = new Grpc::MockAsyncClientFactory;
    async_client_ = new Grpc::MockAsyncClient;
    EXPECT_CALL(async_client_manager_, factoryForGrpcService(_, _, false))
        .WillOnce(Invoke([this](const envoy::api::v2::core::GrpcService&, Stats::Scope&, bool) {
          EXPECT_CALL(*factory_, create()).WillOnce(Invoke([this] {
            return Grpc::RawAsyncClientPtr{async_client_};
          }));
          return Grpc::AsyncClientFactoryPtr{factory_};
        }));
  }

  LocalInfo::MockLocalInfo local_info_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Grpc::MockAsyncClientManager async_client_manager_;
  Grpc::MockAsyncClient* async_client_ = nullptr;
  Grpc::MockAsyncClientFactory* factory_ = nullptr;
  std::unique_ptr<GrpcAccessLoggerCacheImpl> logger_cache_;
  NiceMock<Stats::MockIsolatedStatsStore> scope_;
};

TEST_F(GrpcAccessLoggerCacheImplTest, Deduplication) {
  InSequence s;

  ::envoy::config::accesslog::v2::CommonGrpcAccessLogConfig config;
  config.set_log_name("log-1");
  config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("cluster-1");

  expectClientCreation();
  GrpcAccessLoggerSharedPtr logger1 = logger_cache_->getOrCreateLogger(config);
  EXPECT_EQ(logger1, logger_cache_->getOrCreateLogger(config));

  // Changing log name leads to another logger.
  config.set_log_name("log-2");
  expectClientCreation();
  EXPECT_NE(logger1, logger_cache_->getOrCreateLogger(config));

  config.set_log_name("log-1");
  EXPECT_EQ(logger1, logger_cache_->getOrCreateLogger(config));

  // Changing cluster name leads to another logger.
  config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("cluster-2");
  expectClientCreation();
  EXPECT_NE(logger1, logger_cache_->getOrCreateLogger(config));
}

class MockGrpcAccessLogger : public GrpcAccessLogger {
public:
  // GrpcAccessLogger
  MOCK_METHOD1(log, void(envoy::data::accesslog::v2::HTTPAccessLogEntry&& entry));
};

class MockGrpcAccessLoggerCache : public GrpcAccessLoggerCache {
public:
  // GrpcAccessLoggerCache
  MOCK_METHOD1(getOrCreateLogger,
               GrpcAccessLoggerSharedPtr(
                   const ::envoy::config::accesslog::v2::CommonGrpcAccessLogConfig& config));
};

class HttpGrpcAccessLogTest : public testing::Test {
public:
  void init() {
    ON_CALL(*filter_, evaluate(_, _, _, _)).WillByDefault(Return(true));
    config_.mutable_common_config()->set_log_name("hello_log");
    EXPECT_CALL(*logger_cache_, getOrCreateLogger(_))
        .WillOnce([this](const ::envoy::config::accesslog::v2::CommonGrpcAccessLogConfig& config) {
          EXPECT_EQ(config.DebugString(), config_.common_config().DebugString());
          return logger_;
        });
    access_log_ = std::make_unique<HttpGrpcAccessLog>(AccessLog::FilterPtr{filter_}, config_, tls_,
                                                      logger_cache_);
  }

  void expectLog(const std::string& expected_log_entry_yaml) {
    if (access_log_ == nullptr) {
      init();
    }

    envoy::data::accesslog::v2::HTTPAccessLogEntry expected_log_entry;
    TestUtility::loadFromYaml(expected_log_entry_yaml, expected_log_entry);
    EXPECT_CALL(*logger_, log(_))
        .WillOnce(
            Invoke([expected_log_entry](envoy::data::accesslog::v2::HTTPAccessLogEntry&& entry) {
              EXPECT_EQ(entry.DebugString(), expected_log_entry.DebugString());
            }));
  }

  void expectLogRequestMethod(const std::string& request_method) {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.host_ = nullptr;

    Http::TestHeaderMapImpl request_headers{
        {":method", request_method},
    };

    expectLog(fmt::format(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
  start_time: {{}}
request:
  request_method: {}
  request_headers_bytes: {}
response: {{}}
    )EOF",
                          request_method, request_method.length() + 7));
    access_log_->log(&request_headers, nullptr, nullptr, stream_info);
  }

  AccessLog::MockFilter* filter_{new NiceMock<AccessLog::MockFilter>()};
  NiceMock<ThreadLocal::MockInstance> tls_;
  envoy::config::accesslog::v2::HttpGrpcAccessLogConfig config_;
  std::shared_ptr<MockGrpcAccessLogger> logger_{new MockGrpcAccessLogger()};
  std::shared_ptr<MockGrpcAccessLoggerCache> logger_cache_{new MockGrpcAccessLoggerCache()};
  std::unique_ptr<HttpGrpcAccessLog> access_log_;
};

// Test HTTP log marshalling.
TEST_F(HttpGrpcAccessLogTest, Marshalling) {
  InSequence s;

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.host_ = nullptr;
    stream_info.start_time_ = SystemTime(1h);
    stream_info.start_time_monotonic_ = MonotonicTime(1h);
    stream_info.last_downstream_tx_byte_sent_ = 2ms;
    stream_info.setDownstreamLocalAddress(std::make_shared<Network::Address::PipeInstance>("/foo"));
    (*stream_info.metadata_.mutable_filter_metadata())["foo"] = ProtobufWkt::Struct();

    expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  downstream_local_address:
    pipe:
      path: "/foo"
  start_time:
    seconds: 3600
  time_to_last_downstream_tx_byte:
    nanos: 2000000
  metadata:
    filter_metadata:
      foo: {}
request: {}
response: {}
)EOF");
    access_log_->log(nullptr, nullptr, nullptr, stream_info);
  }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.host_ = nullptr;
    stream_info.start_time_ = SystemTime(1h);
    stream_info.last_downstream_tx_byte_sent_ = std::chrono::nanoseconds(2000000);

    expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
  start_time:
    seconds: 3600
  time_to_last_downstream_tx_byte:
    nanos: 2000000
request: {}
response: {}
)EOF");
    access_log_->log(nullptr, nullptr, nullptr, stream_info);
  }

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

    Http::TestHeaderMapImpl request_headers{
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
    Http::TestHeaderMapImpl response_headers{{":status", "200"}};

    expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
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
    access_log_->log(&request_headers, &response_headers, nullptr, stream_info);
  }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.host_ = nullptr;
    stream_info.start_time_ = SystemTime(1h);
    stream_info.upstream_transport_failure_reason_ = "TLS error";

    Http::TestHeaderMapImpl request_headers{
        {":method", "WHACKADOO"},
    };

    expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
  start_time:
    seconds: 3600
  upstream_transport_failure_reason: "TLS error"
request:
  request_method: "METHOD_UNSPECIFIED"
  request_headers_bytes: 16
response: {}
)EOF");
    access_log_->log(&request_headers, nullptr, nullptr, stream_info);
  }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.host_ = nullptr;
    stream_info.start_time_ = SystemTime(1h);

    NiceMock<Ssl::MockConnectionInfo> connection_info;
    const std::vector<std::string> peerSans{"peerSan1", "peerSan2"};
    ON_CALL(connection_info, uriSanPeerCertificate()).WillByDefault(Return(peerSans));
    const std::vector<std::string> localSans{"localSan1", "localSan2"};
    ON_CALL(connection_info, uriSanLocalCertificate()).WillByDefault(Return(localSans));
    ON_CALL(connection_info, subjectPeerCertificate()).WillByDefault(Return("peerSubject"));
    ON_CALL(connection_info, subjectLocalCertificate()).WillByDefault(Return("localSubject"));
    ON_CALL(connection_info, sessionId())
        .WillByDefault(Return("D62A523A65695219D46FE1FFE285A4C371425ACE421B110B5B8D11D3EB4D5F0B"));
    ON_CALL(connection_info, tlsVersion()).WillByDefault(Return("TLSv1.3"));
    ON_CALL(connection_info, ciphersuiteId()).WillByDefault(Return(0x2CC0));
    stream_info.setDownstreamSslConnection(&connection_info);
    stream_info.requested_server_name_ = "sni";

    Http::TestHeaderMapImpl request_headers{
        {":method", "WHACKADOO"},
    };

    expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
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
    tls_session_id: D62A523A65695219D46FE1FFE285A4C371425ACE421B110B5B8D11D3EB4D5F0B
request:
  request_method: "METHOD_UNSPECIFIED"
  request_headers_bytes: 16
response: {}
)EOF");
    access_log_->log(&request_headers, nullptr, nullptr, stream_info);
  }

  // TLSv1.2
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.host_ = nullptr;
    stream_info.start_time_ = SystemTime(1h);

    NiceMock<Ssl::MockConnectionInfo> connection_info;
    ON_CALL(connection_info, tlsVersion()).WillByDefault(Return("TLSv1.2"));
    ON_CALL(connection_info, ciphersuiteId()).WillByDefault(Return(0x2F));
    stream_info.setDownstreamSslConnection(&connection_info);
    stream_info.requested_server_name_ = "sni";

    Http::TestHeaderMapImpl request_headers{
        {":method", "WHACKADOO"},
    };

    expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
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
    access_log_->log(nullptr, nullptr, nullptr, stream_info);
  }

  // TLSv1.1
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.host_ = nullptr;
    stream_info.start_time_ = SystemTime(1h);

    NiceMock<Ssl::MockConnectionInfo> connection_info;
    ON_CALL(connection_info, tlsVersion()).WillByDefault(Return("TLSv1.1"));
    ON_CALL(connection_info, ciphersuiteId()).WillByDefault(Return(0x2F));
    stream_info.setDownstreamSslConnection(&connection_info);
    stream_info.requested_server_name_ = "sni";

    Http::TestHeaderMapImpl request_headers{
        {":method", "WHACKADOO"},
    };

    expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
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
    access_log_->log(nullptr, nullptr, nullptr, stream_info);
  }

  // TLSv1
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.host_ = nullptr;
    stream_info.start_time_ = SystemTime(1h);

    NiceMock<Ssl::MockConnectionInfo> connection_info;
    ON_CALL(connection_info, tlsVersion()).WillByDefault(Return("TLSv1"));
    ON_CALL(connection_info, ciphersuiteId()).WillByDefault(Return(0x2F));
    stream_info.setDownstreamSslConnection(&connection_info);
    stream_info.requested_server_name_ = "sni";

    Http::TestHeaderMapImpl request_headers{
        {":method", "WHACKADOO"},
    };

    expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
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
    access_log_->log(nullptr, nullptr, nullptr, stream_info);
  }

  // Unknown TLS version (TLSv1.4)
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.host_ = nullptr;
    stream_info.start_time_ = SystemTime(1h);

    NiceMock<Ssl::MockConnectionInfo> connection_info;
    ON_CALL(connection_info, tlsVersion()).WillByDefault(Return("TLSv1.4"));
    ON_CALL(connection_info, ciphersuiteId()).WillByDefault(Return(0x2F));
    stream_info.setDownstreamSslConnection(&connection_info);
    stream_info.requested_server_name_ = "sni";

    Http::TestHeaderMapImpl request_headers{
        {":method", "WHACKADOO"},
    };

    expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
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
    access_log_->log(nullptr, nullptr, nullptr, stream_info);
  }
}

// Test HTTP log marshalling with additional headers.
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
    stream_info.host_ = nullptr;
    stream_info.start_time_ = SystemTime(1h);

    Http::TestHeaderMapImpl request_headers{
        {":scheme", "scheme_value"},
        {":authority", "authority_value"},
        {":path", "path_value"},
        {":method", "POST"},
        {"x-envoy-max-retries", "3"}, // test inline header not otherwise logged
        {"x-custom-request", "custom_value"},
        {"x-custom-empty", ""},
    };
    Http::TestHeaderMapImpl response_headers{
        {":status", "200"},
        {"x-envoy-immediate-health-check-fail", "true"}, // test inline header not otherwise logged
        {"x-custom-response", "custom_value"},
        {"x-custom-empty", ""},
    };

    Http::TestHeaderMapImpl response_trailers{
        {"x-logged-trailer", "value"},
        {"x-empty-trailer", ""},
        {"x-unlogged-trailer", "2"},
    };

    expectLog(R"EOF(
common_properties:
  downstream_remote_address:
    socket_address:
      address: "127.0.0.1"
      port_value: 0
  downstream_local_address:
    socket_address:
      address: "127.0.0.2"
      port_value: 0
  start_time:
    seconds: 3600
request:
  scheme: "scheme_value"
  authority: "authority_value"
  path: "path_value"
  request_method: "POST"
  request_headers_bytes: 132
  request_headers:
    "x-custom-request": "custom_value"
    "x-custom-empty": ""
    "x-envoy-max-retries": "3"
response:
  response_headers_bytes: 92
  response_headers:
    "x-custom-response": "custom_value"
    "x-custom-empty": ""
    "x-envoy-immediate-health-check-fail": "true"
  response_trailers:
    "x-logged-trailer": "value"
    "x-empty-trailer": ""
)EOF");
    access_log_->log(&request_headers, &response_headers, &response_trailers, stream_info);
  }
}

TEST(responseFlagsToAccessLogResponseFlagsTest, All) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(stream_info, hasResponseFlag(_)).WillByDefault(Return(true));
  envoy::data::accesslog::v2::AccessLogCommon common_access_log;
  HttpGrpcAccessLog::responseFlagsToAccessLogResponseFlags(common_access_log, stream_info);

  envoy::data::accesslog::v2::AccessLogCommon common_access_log_expected;
  common_access_log_expected.mutable_response_flags()->set_failed_local_healthcheck(true);
  common_access_log_expected.mutable_response_flags()->set_no_healthy_upstream(true);
  common_access_log_expected.mutable_response_flags()->set_upstream_request_timeout(true);
  common_access_log_expected.mutable_response_flags()->set_local_reset(true);
  common_access_log_expected.mutable_response_flags()->set_upstream_remote_reset(true);
  common_access_log_expected.mutable_response_flags()->set_upstream_connection_failure(true);
  common_access_log_expected.mutable_response_flags()->set_upstream_connection_termination(true);
  common_access_log_expected.mutable_response_flags()->set_upstream_overflow(true);
  common_access_log_expected.mutable_response_flags()->set_no_route_found(true);
  common_access_log_expected.mutable_response_flags()->set_delay_injected(true);
  common_access_log_expected.mutable_response_flags()->set_fault_injected(true);
  common_access_log_expected.mutable_response_flags()->set_rate_limited(true);
  common_access_log_expected.mutable_response_flags()->mutable_unauthorized_details()->set_reason(
      envoy::data::accesslog::v2::ResponseFlags_Unauthorized_Reason::
          ResponseFlags_Unauthorized_Reason_EXTERNAL_SERVICE);
  common_access_log_expected.mutable_response_flags()->set_rate_limit_service_error(true);
  common_access_log_expected.mutable_response_flags()->set_downstream_connection_termination(true);
  common_access_log_expected.mutable_response_flags()->set_upstream_retry_limit_exceeded(true);
  common_access_log_expected.mutable_response_flags()->set_stream_idle_timeout(true);
  common_access_log_expected.mutable_response_flags()->set_invalid_envoy_request_headers(true);

  EXPECT_EQ(common_access_log_expected.DebugString(), common_access_log.DebugString());
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

} // namespace
} // namespace HttpGrpc
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
