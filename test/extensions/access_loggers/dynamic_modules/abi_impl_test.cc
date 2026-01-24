#include "source/common/network/address_impl.h"
#include "source/extensions/access_loggers/dynamic_modules/access_log.h"
#include "source/extensions/dynamic_modules/abi.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace DynamicModules {
namespace {

class MockStreamIdProvider : public StreamInfo::StreamIdProvider {
public:
  MOCK_METHOD(absl::optional<absl::string_view>, toStringView, (), (const));
  MOCK_METHOD(absl::optional<uint64_t>, toInteger, (), (const));
};

class DynamicModuleAccessLogAbiTest : public testing::Test {
public:
  void SetUp() override {
    stream_info_.response_code_ = 200;
    stream_info_.protocol_ = Http::Protocol::Http11;
  }

  void* createThreadLocalLogger(const Formatter::Context& context,
                                const StreamInfo::StreamInfo& stream_info) {
    logger_ = std::make_unique<ThreadLocalLogger>(nullptr, nullptr, 1);
    logger_->log_context_ = &context;
    logger_->stream_info_ = &stream_info;
    return static_cast<void*>(logger_.get());
  }

  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  std::unique_ptr<ThreadLocalLogger> logger_;
  Http::TestRequestHeaderMapImpl request_headers_{{"x-request-id", "req-123"},
                                                  {"host", "example.com"}};
  Http::TestResponseHeaderMapImpl response_headers_{
      {"content-type", "application/json"}, {"x-custom", "value1"}, {"x-custom", "value2"}};
  Http::TestResponseTrailerMapImpl response_trailers_{{"x-trailer", "trailer-value"}};
};

// =============================================================================
// Header Access Tests
// =============================================================================

TEST_F(DynamicModuleAccessLogAbiTest, HeadersSizeRequestHeaders) {
  Formatter::Context log_context(&request_headers_, &response_headers_, &response_trailers_);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  EXPECT_EQ(2, envoy_dynamic_module_callback_access_logger_get_headers_size(
                   env_ptr, envoy_dynamic_module_type_http_header_type_RequestHeader));
}

TEST_F(DynamicModuleAccessLogAbiTest, HeadersSizeResponseHeaders) {
  Formatter::Context log_context(&request_headers_, &response_headers_, &response_trailers_);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  // 3 headers: content-type, x-custom, x-custom.
  EXPECT_EQ(3, envoy_dynamic_module_callback_access_logger_get_headers_size(
                   env_ptr, envoy_dynamic_module_type_http_header_type_ResponseHeader));
}

TEST_F(DynamicModuleAccessLogAbiTest, HeadersSizeResponseTrailers) {
  Formatter::Context log_context(&request_headers_, &response_headers_, &response_trailers_);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  EXPECT_EQ(1, envoy_dynamic_module_callback_access_logger_get_headers_size(
                   env_ptr, envoy_dynamic_module_type_http_header_type_ResponseTrailer));
}

TEST_F(DynamicModuleAccessLogAbiTest, HeadersSizeNullHeaders) {
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  EXPECT_EQ(0, envoy_dynamic_module_callback_access_logger_get_headers_size(
                   env_ptr, envoy_dynamic_module_type_http_header_type_RequestHeader));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetHeaders) {
  Formatter::Context log_context(&request_headers_, &response_headers_, &response_trailers_);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  std::vector<envoy_dynamic_module_type_envoy_http_header> headers(2);
  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_get_headers(
      env_ptr, envoy_dynamic_module_type_http_header_type_RequestHeader, headers.data()));

  // Order isn't guaranteed by map iteration, but for small maps typically consistent.
  // We just verify the contents exist.
  std::vector<std::pair<std::string, std::string>> result;
  result.reserve(headers.size());
  for (const auto& h : headers) {
    result.push_back(
        {std::string(h.key_ptr, h.key_length), std::string(h.value_ptr, h.value_length)});
  }
  EXPECT_THAT(result, testing::UnorderedElementsAre(testing::Pair("x-request-id", "req-123"),
                                                    testing::Pair(":authority", "example.com")));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetHeadersNull) {
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_get_headers(
      env_ptr, envoy_dynamic_module_type_http_header_type_RequestHeader, nullptr));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetHeaderValueFound) {
  Formatter::Context log_context(&request_headers_, &response_headers_, &response_trailers_);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_module_buffer key = {"x-request-id", 12};
  envoy_dynamic_module_type_envoy_buffer result;
  size_t count = 0;

  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_get_header_value(
      env_ptr, envoy_dynamic_module_type_http_header_type_RequestHeader, key, &result, 0, &count));
  EXPECT_EQ("req-123", std::string(result.ptr, result.length));
  EXPECT_EQ(1, count);
}

TEST_F(DynamicModuleAccessLogAbiTest, GetHeaderValueMultiValue) {
  Formatter::Context log_context(&request_headers_, &response_headers_, &response_trailers_);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_module_buffer key = {"x-custom", 8};
  envoy_dynamic_module_type_envoy_buffer result;
  size_t count = 0;

  // Get first value.
  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_get_header_value(
      env_ptr, envoy_dynamic_module_type_http_header_type_ResponseHeader, key, &result, 0, &count));
  EXPECT_EQ("value1", std::string(result.ptr, result.length));
  EXPECT_EQ(2, count);

  // Get second value.
  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_get_header_value(
      env_ptr, envoy_dynamic_module_type_http_header_type_ResponseHeader, key, &result, 1, &count));
  EXPECT_EQ("value2", std::string(result.ptr, result.length));
  EXPECT_EQ(2, count);
}

TEST_F(DynamicModuleAccessLogAbiTest, GetHeaderValueNotFound) {
  Formatter::Context log_context(&request_headers_, &response_headers_, &response_trailers_);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_module_buffer key = {"nonexistent", 11};
  envoy_dynamic_module_type_envoy_buffer result;
  size_t count = 0;

  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_get_header_value(
      env_ptr, envoy_dynamic_module_type_http_header_type_RequestHeader, key, &result, 0, &count));
  EXPECT_EQ(0, count);
}

TEST_F(DynamicModuleAccessLogAbiTest, GetHeaderValueIndexOutOfBounds) {
  Formatter::Context log_context(&request_headers_, &response_headers_, &response_trailers_);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_module_buffer key = {"x-request-id", 12};
  envoy_dynamic_module_type_envoy_buffer result;
  size_t count = 0;

  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_get_header_value(
      env_ptr, envoy_dynamic_module_type_http_header_type_RequestHeader, key, &result, 1, &count));
  EXPECT_EQ(1, count);
}

// =============================================================================
// Stream Info Basic Tests
// =============================================================================

TEST_F(DynamicModuleAccessLogAbiTest, GetResponseCode) {
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  EXPECT_EQ(envoy_dynamic_module_callback_access_logger_get_response_code(env_ptr), 200);
}

TEST_F(DynamicModuleAccessLogAbiTest, GetResponseCodeNotSet) {
  stream_info_.response_code_ = absl::nullopt;
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  EXPECT_EQ(envoy_dynamic_module_callback_access_logger_get_response_code(env_ptr), 0);
}

TEST_F(DynamicModuleAccessLogAbiTest, GetResponseCodeDetails) {
  stream_info_.response_code_details_ = "details";
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_TRUE(
      envoy_dynamic_module_callback_access_logger_get_response_code_details(env_ptr, &result));
  EXPECT_EQ("details", std::string(result.ptr, result.length));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetResponseCodeDetailsNotSet) {
  stream_info_.response_code_details_ = absl::nullopt;
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_FALSE(
      envoy_dynamic_module_callback_access_logger_get_response_code_details(env_ptr, &result));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetProtocol) {
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer protocol;
  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_get_protocol(env_ptr, &protocol));
  EXPECT_EQ("HTTP/1.1", std::string(protocol.ptr, protocol.length));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetProtocolHttp2) {
  stream_info_.protocol_ = Http::Protocol::Http2;
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer protocol;
  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_get_protocol(env_ptr, &protocol));
  EXPECT_EQ("HTTP/2", std::string(protocol.ptr, protocol.length));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetResponseFlags) {
  stream_info_.setResponseFlag(StreamInfo::CoreResponseFlag::UpstreamConnectionFailure);
  stream_info_.setResponseFlag(StreamInfo::CoreResponseFlag::NoRouteFound);
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_has_response_flag(
      env_ptr, envoy_dynamic_module_type_response_flag_UpstreamConnectionFailure));
  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_has_response_flag(
      env_ptr, envoy_dynamic_module_type_response_flag_NoRouteFound));
  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_has_response_flag(
      env_ptr, envoy_dynamic_module_type_response_flag_RateLimited));

  uint64_t flags = envoy_dynamic_module_callback_access_logger_get_response_flags(env_ptr);
  EXPECT_EQ(stream_info_.legacyResponseFlags(), flags);
}

TEST_F(DynamicModuleAccessLogAbiTest, GetRouteName) {
  ON_CALL(stream_info_, getRouteName())
      .WillByDefault(testing::ReturnRefOfCopy(std::string("test_route")));
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_get_route_name(env_ptr, &result));
  EXPECT_EQ("test_route", std::string(result.ptr, result.length));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetRouteNameEmpty) {
  ON_CALL(stream_info_, getRouteName()).WillByDefault(testing::ReturnRefOfCopy(std::string("")));
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_get_route_name(env_ptr, &result));
}

TEST_F(DynamicModuleAccessLogAbiTest, IsHealthCheck) {
  ON_CALL(stream_info_, healthCheck()).WillByDefault(testing::Return(true));
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_is_health_check(env_ptr));
}

TEST_F(DynamicModuleAccessLogAbiTest, IsNotHealthCheck) {
  ON_CALL(stream_info_, healthCheck()).WillByDefault(testing::Return(false));
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_is_health_check(env_ptr));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetAttemptCount) {
  stream_info_.attempt_count_ = 3;
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  EXPECT_EQ(envoy_dynamic_module_callback_access_logger_get_attempt_count(env_ptr), 3);
}

TEST_F(DynamicModuleAccessLogAbiTest, GetAttemptCountNotSet) {
  stream_info_.attempt_count_ = absl::nullopt;
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  EXPECT_EQ(envoy_dynamic_module_callback_access_logger_get_attempt_count(env_ptr), 0);
}

// =============================================================================
// Timing and Bytes Info Tests
// =============================================================================

TEST_F(DynamicModuleAccessLogAbiTest, GetTimingInfo) {
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_timing_info timing;
  envoy_dynamic_module_callback_access_logger_get_timing_info(env_ptr, &timing);

  // At minimum, start time should be set.
  EXPECT_GE(timing.start_time_unix_ns, 0);
}

TEST_F(DynamicModuleAccessLogAbiTest, GetTimingInfoNoDownstreamOrUpstream) {
  // Force downstream timing to null (const overload).
  ON_CALL(Const(stream_info_), downstreamTiming())
      .WillByDefault(testing::Return(OptRef<const StreamInfo::DownstreamTiming>()));
  // Force upstream info to null.
  ON_CALL(stream_info_, upstreamInfo())
      .WillByDefault(testing::Return(std::shared_ptr<StreamInfo::UpstreamInfo>()));
  ON_CALL(Const(stream_info_), upstreamInfo())
      .WillByDefault(testing::Return(OptRef<const StreamInfo::UpstreamInfo>()));

  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_timing_info timing;
  envoy_dynamic_module_callback_access_logger_get_timing_info(env_ptr, &timing);

  EXPECT_EQ(-1, timing.first_downstream_tx_byte_sent_ns);
  EXPECT_EQ(-1, timing.last_downstream_tx_byte_sent_ns);
  EXPECT_EQ(-1, timing.first_upstream_tx_byte_sent_ns);
  EXPECT_EQ(-1, timing.last_upstream_tx_byte_sent_ns);
  EXPECT_EQ(-1, timing.first_upstream_rx_byte_received_ns);
  EXPECT_EQ(-1, timing.last_upstream_rx_byte_received_ns);
}

TEST_F(DynamicModuleAccessLogAbiTest, GetTimingInfoWithValues) {
  // Set start and completion times.
  stream_info_.start_time_monotonic_ = MonotonicTime(std::chrono::seconds(1));
  stream_info_.start_time_ = SystemTime(std::chrono::seconds(10));
  stream_info_.end_time_ = std::chrono::nanoseconds(5'000'000); // 5 ms

  // Downstream timing.
  stream_info_.downstream_timing_.first_downstream_tx_byte_sent_ =
      stream_info_.start_time_monotonic_ + std::chrono::milliseconds(2);
  stream_info_.downstream_timing_.last_downstream_tx_byte_sent_ =
      stream_info_.start_time_monotonic_ + std::chrono::milliseconds(3);

  // Upstream timing.
  auto* upstream =
      dynamic_cast<NiceMock<StreamInfo::MockUpstreamInfo>*>(stream_info_.upstream_info_.get());
  ASSERT_NE(upstream, nullptr);
  upstream->upstream_timing_.first_upstream_tx_byte_sent_ =
      stream_info_.start_time_monotonic_ + std::chrono::milliseconds(4);
  upstream->upstream_timing_.last_upstream_tx_byte_sent_ =
      stream_info_.start_time_monotonic_ + std::chrono::milliseconds(5);
  upstream->upstream_timing_.first_upstream_rx_byte_received_ =
      stream_info_.start_time_monotonic_ + std::chrono::milliseconds(6);
  upstream->upstream_timing_.last_upstream_rx_byte_received_ =
      stream_info_.start_time_monotonic_ + std::chrono::milliseconds(7);

  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_timing_info timing;
  envoy_dynamic_module_callback_access_logger_get_timing_info(env_ptr, &timing);

  EXPECT_EQ(5'000'000, timing.request_complete_duration_ns); // 5 ms
  EXPECT_EQ(2'000'000, timing.first_downstream_tx_byte_sent_ns);
  EXPECT_EQ(3'000'000, timing.last_downstream_tx_byte_sent_ns);
  EXPECT_EQ(4'000'000, timing.first_upstream_tx_byte_sent_ns);
  EXPECT_EQ(5'000'000, timing.last_upstream_tx_byte_sent_ns);
  EXPECT_EQ(6'000'000, timing.first_upstream_rx_byte_received_ns);
  EXPECT_EQ(7'000'000, timing.last_upstream_rx_byte_received_ns);
}

TEST_F(DynamicModuleAccessLogAbiTest, GetBytesInfo) {
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_bytes_info bytes;
  envoy_dynamic_module_callback_access_logger_get_bytes_info(env_ptr, &bytes);

  // These should be zeroes for our mock.
  EXPECT_EQ(0, bytes.bytes_received);
  EXPECT_EQ(0, bytes.bytes_sent);
}

TEST_F(DynamicModuleAccessLogAbiTest, GetBytesInfoWithUpstreamBytesMeter) {
  auto meter = std::make_shared<StreamInfo::BytesMeter>();
  meter->addWireBytesReceived(123);
  meter->addWireBytesSent(456);
  stream_info_.setUpstreamBytesMeter(meter);

  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_bytes_info bytes;
  envoy_dynamic_module_callback_access_logger_get_bytes_info(env_ptr, &bytes);

  EXPECT_EQ(123, bytes.wire_bytes_received);
  EXPECT_EQ(456, bytes.wire_bytes_sent);
}

// =============================================================================
// Upstream Info and Transport Failure Tests
// =============================================================================

TEST_F(DynamicModuleAccessLogAbiTest, GetUpstreamCluster) {
  auto cluster_info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  ON_CALL(*cluster_info, name()).WillByDefault(testing::ReturnRefOfCopy(std::string("cluster-a")));
  stream_info_.setUpstreamClusterInfo(cluster_info);

  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_get_upstream_cluster(env_ptr, &result));
  EXPECT_EQ("cluster-a", std::string(result.ptr, result.length));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetUpstreamClusterMissing) {
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_get_upstream_cluster(env_ptr, &result));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetUpstreamHost) {
  auto upstream_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
  ON_CALL(*upstream_host, hostname())
      .WillByDefault(testing::ReturnRefOfCopy(std::string("host-a")));
  stream_info_.upstream_info_->setUpstreamHost(upstream_host);

  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_get_upstream_host(env_ptr, &result));
  EXPECT_EQ("host-a", std::string(result.ptr, result.length));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetUpstreamHostMissing) {
  stream_info_.upstream_info_->setUpstreamHost(Upstream::HostDescriptionConstSharedPtr{});

  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_get_upstream_host(env_ptr, &result));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetUpstreamTransportFailureReason) {
  stream_info_.upstream_info_->setUpstreamTransportFailureReason("refused");

  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_get_upstream_transport_failure_reason(
      env_ptr, &result));
  EXPECT_EQ("refused", std::string(result.ptr, result.length));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetUpstreamTransportFailureReasonMissing) {
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_get_upstream_transport_failure_reason(
      env_ptr, &result));
}

// =============================================================================
// Connection / TLS Info Tests
// =============================================================================

TEST_F(DynamicModuleAccessLogAbiTest, GetConnectionId) {
  stream_info_.downstream_connection_info_provider_->setConnectionID(98765);

  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  EXPECT_EQ(98765, envoy_dynamic_module_callback_access_logger_get_connection_id(env_ptr));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetConnectionIdMissing) {
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  EXPECT_EQ(0, envoy_dynamic_module_callback_access_logger_get_connection_id(env_ptr));
}

TEST_F(DynamicModuleAccessLogAbiTest, DownstreamTlsFields) {
  auto ssl_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*ssl_info, peerCertificateValidated()).WillByDefault(testing::Return(true));
  ON_CALL(*ssl_info, tlsVersion()).WillByDefault(testing::ReturnRefOfCopy(std::string("TLSv1.2")));
  ON_CALL(*ssl_info, subjectPeerCertificate())
      .WillByDefault(testing::ReturnRefOfCopy(std::string("CN=subj")));
  ON_CALL(*ssl_info, sha256PeerCertificateDigest())
      .WillByDefault(testing::ReturnRefOfCopy(std::string("digest123")));

  stream_info_.downstream_connection_info_provider_->setSslConnection(ssl_info);
  stream_info_.downstream_connection_info_provider_->setRequestedServerName("example.test");

  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer buf;
  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_get_requested_server_name(env_ptr, &buf));
  EXPECT_EQ("example.test", std::string(buf.ptr, buf.length));

  EXPECT_TRUE(
      envoy_dynamic_module_callback_access_logger_get_downstream_tls_version(env_ptr, &buf));
  EXPECT_EQ("TLSv1.2", std::string(buf.ptr, buf.length));

  EXPECT_TRUE(
      envoy_dynamic_module_callback_access_logger_get_downstream_peer_subject(env_ptr, &buf));
  EXPECT_EQ("CN=subj", std::string(buf.ptr, buf.length));

  EXPECT_TRUE(
      envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_digest(env_ptr, &buf));
  EXPECT_EQ("digest123", std::string(buf.ptr, buf.length));

  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_is_mtls(env_ptr));
}

TEST_F(DynamicModuleAccessLogAbiTest, DownstreamTlsMissing) {
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer buf;
  EXPECT_FALSE(
      envoy_dynamic_module_callback_access_logger_get_requested_server_name(env_ptr, &buf));
  EXPECT_FALSE(
      envoy_dynamic_module_callback_access_logger_get_downstream_tls_version(env_ptr, &buf));
  EXPECT_FALSE(
      envoy_dynamic_module_callback_access_logger_get_downstream_peer_subject(env_ptr, &buf));
  EXPECT_FALSE(
      envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_digest(env_ptr, &buf));
  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_is_mtls(env_ptr));
}

// =============================================================================
// Request ID and Filter State Tests
// =============================================================================

TEST_F(DynamicModuleAccessLogAbiTest, GetRequestIdMissingProvider) {
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_get_request_id(env_ptr, &result));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetFilterStateAlwaysFalse) {
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_module_buffer key = {"k", 1};
  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_get_filter_state(env_ptr, key, &result));
}

// =============================================================================
// Address Info Tests
// =============================================================================

TEST_F(DynamicModuleAccessLogAbiTest, GetDownstreamAddresses) {
  auto local_addr = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance("127.0.0.1", 8080)};
  auto remote_addr = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance("10.0.0.1", 12345)};

  stream_info_.downstream_connection_info_provider_->setLocalAddress(local_addr);
  stream_info_.downstream_connection_info_provider_->setRemoteAddress(remote_addr);

  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer addr;
  uint32_t port;

  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_get_downstream_local_address(
      env_ptr, &addr, &port));
  EXPECT_EQ("127.0.0.1", std::string(addr.ptr, addr.length));
  EXPECT_EQ(8080, port);

  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_get_downstream_remote_address(
      env_ptr, &addr, &port));
  EXPECT_EQ("10.0.0.1", std::string(addr.ptr, addr.length));
  EXPECT_EQ(12345, port);
}

TEST_F(DynamicModuleAccessLogAbiTest, GetDownstreamRemoteAddressNonIp) {
  auto non_ip =
      Network::Address::InstanceConstSharedPtr(new Network::Address::EnvoyInternalInstance(
          "internal-remote", "", &Network::SocketInterfaceSingleton::get()));
  stream_info_.downstream_connection_info_provider_->setRemoteAddress(non_ip);

  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer addr;
  uint32_t port;
  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_get_downstream_remote_address(
      env_ptr, &addr, &port));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetUpstreamAddresses) {
  auto local_addr = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance("192.168.1.2", 20000)};
  auto remote_addr = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance("192.168.1.1", 80)};

  auto upstream_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
  ON_CALL(*upstream_host, address()).WillByDefault(testing::Return(remote_addr));

  stream_info_.upstream_info_->setUpstreamLocalAddress(local_addr);
  stream_info_.upstream_info_->setUpstreamHost(upstream_host);

  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer addr;
  uint32_t port;

  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_get_upstream_local_address(env_ptr, &addr,
                                                                                     &port));
  EXPECT_EQ("192.168.1.2", std::string(addr.ptr, addr.length));
  EXPECT_EQ(20000, port);

  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_get_upstream_remote_address(
      env_ptr, &addr, &port));
  EXPECT_EQ("192.168.1.1", std::string(addr.ptr, addr.length));
  EXPECT_EQ(80, port);
}

TEST_F(DynamicModuleAccessLogAbiTest, GetUpstreamRemoteAddressNonIp) {
  auto non_ip =
      Network::Address::InstanceConstSharedPtr(new Network::Address::EnvoyInternalInstance(
          "internal-upstream", "", &Network::SocketInterfaceSingleton::get()));
  auto upstream_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
  ON_CALL(*upstream_host, address()).WillByDefault(testing::Return(non_ip));
  stream_info_.upstream_info_->setUpstreamHost(upstream_host);

  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer addr;
  uint32_t port;
  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_get_upstream_remote_address(
      env_ptr, &addr, &port));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetUpstreamRemoteAddressMissingUpstream) {
  stream_info_.setUpstreamInfo(std::shared_ptr<StreamInfo::UpstreamInfo>());
  ON_CALL(stream_info_, upstreamInfo())
      .WillByDefault(testing::Return(std::shared_ptr<StreamInfo::UpstreamInfo>()));
  ON_CALL(Const(stream_info_), upstreamInfo())
      .WillByDefault(testing::Return(OptRef<const StreamInfo::UpstreamInfo>()));

  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer addr;
  uint32_t port;
  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_get_upstream_remote_address(
      env_ptr, &addr, &port));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetUpstreamLocalAddressMissingAndNonIp) {
  // Missing upstream info -> null optional.
  auto upstream_info = std::make_shared<NiceMock<StreamInfo::MockUpstreamInfo>>();
  upstream_info->upstream_local_address_ = Network::Address::InstanceConstSharedPtr{};
  ON_CALL(*upstream_info, upstreamLocalAddress())
      .WillByDefault(testing::ReturnRef(upstream_info->upstream_local_address_));
  stream_info_.setUpstreamInfo(upstream_info);

  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);
  envoy_dynamic_module_type_envoy_buffer addr;
  uint32_t port;
  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_get_upstream_local_address(
      env_ptr, &addr, &port));

  // Non-IP upstream local address.
  auto non_ip =
      Network::Address::InstanceConstSharedPtr(new Network::Address::EnvoyInternalInstance(
          "internal-upstream-local", "", &Network::SocketInterfaceSingleton::get()));
  upstream_info->upstream_local_address_ = non_ip;
  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_get_upstream_local_address(
      env_ptr, &addr, &port));
}

// =============================================================================
// Upstream Info Tests
// =============================================================================

TEST_F(DynamicModuleAccessLogAbiTest, GetUpstreamInfo) {
  auto cluster_info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  ON_CALL(*cluster_info, name()).WillByDefault(testing::ReturnRefOfCopy(std::string("my_cluster")));

  auto upstream_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
  ON_CALL(*upstream_host, hostname()).WillByDefault(testing::ReturnRefOfCopy(std::string("host1")));

  stream_info_.setUpstreamClusterInfo(cluster_info);
  stream_info_.upstream_info_->setUpstreamHost(upstream_host);
  stream_info_.upstream_info_->setUpstreamTransportFailureReason("connection_refused");

  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer result;

  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_get_upstream_cluster(env_ptr, &result));
  EXPECT_EQ("my_cluster", std::string(result.ptr, result.length));

  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_get_upstream_host(env_ptr, &result));
  EXPECT_EQ("host1", std::string(result.ptr, result.length));

  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_get_upstream_transport_failure_reason(
      env_ptr, &result));
  EXPECT_EQ("connection_refused", std::string(result.ptr, result.length));
}

// =============================================================================
// Connection/TLS Tests
// =============================================================================

TEST_F(DynamicModuleAccessLogAbiTest, GetConnectionInfo) {
  stream_info_.downstream_connection_info_provider_->setConnectionID(12345);
  stream_info_.downstream_connection_info_provider_->setRequestedServerName("example.com");

  auto ssl_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*ssl_info, peerCertificateValidated()).WillByDefault(testing::Return(true));
  ON_CALL(*ssl_info, tlsVersion()).WillByDefault(testing::ReturnRefOfCopy(std::string("TLSv1.3")));
  ON_CALL(*ssl_info, subjectPeerCertificate())
      .WillByDefault(testing::ReturnRefOfCopy(std::string("CN=client")));
  ON_CALL(*ssl_info, sha256PeerCertificateDigest())
      .WillByDefault(testing::ReturnRefOfCopy(std::string("digest")));

  stream_info_.downstream_connection_info_provider_->setSslConnection(ssl_info);

  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  EXPECT_EQ(12345, envoy_dynamic_module_callback_access_logger_get_connection_id(env_ptr));

  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_TRUE(
      envoy_dynamic_module_callback_access_logger_get_requested_server_name(env_ptr, &result));
  EXPECT_EQ("example.com", std::string(result.ptr, result.length));

  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_is_mtls(env_ptr));

  EXPECT_TRUE(
      envoy_dynamic_module_callback_access_logger_get_downstream_tls_version(env_ptr, &result));
  EXPECT_EQ("TLSv1.3", std::string(result.ptr, result.length));

  EXPECT_TRUE(
      envoy_dynamic_module_callback_access_logger_get_downstream_peer_subject(env_ptr, &result));
  EXPECT_EQ("CN=client", std::string(result.ptr, result.length));

  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_digest(env_ptr,
                                                                                          &result));
  EXPECT_EQ("digest", std::string(result.ptr, result.length));
}

TEST_F(DynamicModuleAccessLogAbiTest, DownstreamTlsEmptySubjectAndDigest) {
  stream_info_.downstream_connection_info_provider_->setRequestedServerName("");

  auto ssl_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*ssl_info, peerCertificateValidated()).WillByDefault(testing::Return(true));
  ON_CALL(*ssl_info, tlsVersion()).WillByDefault(testing::ReturnRefOfCopy(std::string("TLSv1.3")));
  ON_CALL(*ssl_info, subjectPeerCertificate())
      .WillByDefault(testing::ReturnRefOfCopy(std::string("")));
  ON_CALL(*ssl_info, sha256PeerCertificateDigest())
      .WillByDefault(testing::ReturnRefOfCopy(std::string("")));
  stream_info_.downstream_connection_info_provider_->setSslConnection(ssl_info);

  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_FALSE(
      envoy_dynamic_module_callback_access_logger_get_requested_server_name(env_ptr, &result));
  EXPECT_TRUE(
      envoy_dynamic_module_callback_access_logger_get_downstream_tls_version(env_ptr, &result));
  EXPECT_EQ("TLSv1.3", std::string(result.ptr, result.length));
  EXPECT_FALSE(
      envoy_dynamic_module_callback_access_logger_get_downstream_peer_subject(env_ptr, &result));
  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_digest(
      env_ptr, &result));
}

// =============================================================================
// Metadata and Other Tests
// =============================================================================

TEST_F(DynamicModuleAccessLogAbiTest, GetDynamicMetadata) {
  Protobuf::Struct struct_obj;
  auto& fields = *struct_obj.mutable_fields();
  fields["key"] = ValueUtil::stringValue("value");

  // Manually set metadata on the mock's storage since the setter is mocked.
  (*stream_info_.metadata_.mutable_filter_metadata())["test_filter"] = struct_obj;

  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_module_buffer filter = {"test_filter", 11};
  envoy_dynamic_module_type_module_buffer key = {"key", 3};
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};

  ASSERT_TRUE(envoy_dynamic_module_callback_access_logger_get_dynamic_metadata(env_ptr, filter, key,
                                                                               &result));
  EXPECT_EQ("value", std::string(result.ptr, result.length));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetDynamicMetadataNotSet) {
  // No metadata set; should return false due to KIND_NOT_SET.
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_module_buffer filter = {"test_filter", 11};
  envoy_dynamic_module_type_module_buffer key = {"key", 3};
  envoy_dynamic_module_type_envoy_buffer result{};

  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_get_dynamic_metadata(env_ptr, filter,
                                                                                key, &result));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetDynamicMetadataNonStringValue) {
  Protobuf::Struct struct_obj;
  auto& fields = *struct_obj.mutable_fields();
  fields["key"] = ValueUtil::numberValue(1.23);
  (*stream_info_.metadata_.mutable_filter_metadata())["test_filter"] = struct_obj;

  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_module_buffer filter = {"test_filter", 11};
  envoy_dynamic_module_type_module_buffer key = {"key", 3};
  envoy_dynamic_module_type_envoy_buffer result;

  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_get_dynamic_metadata(env_ptr, filter,
                                                                                key, &result));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetRequestId) {
  auto provider = std::make_shared<NiceMock<MockStreamIdProvider>>();
  ON_CALL(*provider, toStringView())
      .WillByDefault(testing::Return(absl::optional<absl::string_view>("req-id")));

  // Wire up getStreamIdProvider to return our mock provider.
  ON_CALL(stream_info_, getStreamIdProvider())
      .WillByDefault(testing::Return(makeOptRef<const StreamInfo::StreamIdProvider>(*provider)));

  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  ASSERT_TRUE(envoy_dynamic_module_callback_access_logger_get_request_id(env_ptr, &result));
  EXPECT_EQ("req-id", std::string(result.ptr, result.length));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetLocalReplyBody) {
  // Can't easily set local reply body on Formatter::Context since it's const.
  // But we can create a context with a string view.
  Http::TestRequestHeaderMapImpl request_headers;
  std::string body = "local reply";
  Formatter::Context log_context(&request_headers, nullptr, nullptr, body,
                                 AccessLog::AccessLogType::NotSet, nullptr);

  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_get_local_reply_body(env_ptr, &result));
  EXPECT_EQ("local reply", std::string(result.ptr, result.length));
}

TEST_F(DynamicModuleAccessLogAbiTest, GetLocalReplyBodyEmpty) {
  Http::TestRequestHeaderMapImpl request_headers;
  std::string body = "";
  Formatter::Context log_context(&request_headers, nullptr, nullptr, body,
                                 AccessLog::AccessLogType::NotSet, nullptr);

  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_get_local_reply_body(env_ptr, &result));
}
// =============================================================================
// Tracing Tests (Unsupported functionality check)
// =============================================================================

TEST_F(DynamicModuleAccessLogAbiTest, TracingUnsupported) {
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_get_trace_id(env_ptr, &result));
  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_get_span_id(env_ptr, &result));
}

TEST_F(DynamicModuleAccessLogAbiTest, IsTraceSampled) {
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  ON_CALL(stream_info_, traceReason())
      .WillByDefault(testing::Return(Tracing::Reason::NotTraceable));
  EXPECT_FALSE(envoy_dynamic_module_callback_access_logger_is_trace_sampled(env_ptr));

  ON_CALL(stream_info_, traceReason()).WillByDefault(testing::Return(Tracing::Reason::Sampling));
  EXPECT_TRUE(envoy_dynamic_module_callback_access_logger_is_trace_sampled(env_ptr));
}

// =============================================================================
// Misc ABI Callback Tests
// =============================================================================

TEST_F(DynamicModuleAccessLogAbiTest, GetWorkerIndex) {
  Formatter::Context log_context(nullptr, nullptr, nullptr);
  void* env_ptr = createThreadLocalLogger(log_context, stream_info_);

  // The worker_index is set to 1 in createThreadLocalLogger.
  uint32_t worker_index = envoy_dynamic_module_callback_access_logger_get_worker_index(env_ptr);
  EXPECT_EQ(1u, worker_index);
}

} // namespace
} // namespace DynamicModules
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
