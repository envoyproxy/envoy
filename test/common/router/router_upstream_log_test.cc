#include <ctime>
#include <memory>
#include <regex>

#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"

#include "source/common/network/utility.h"
#include "source/common/router/router.h"
#include "source/common/upstream/upstream_impl.h"

#include "test/common/http/common.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Router {
namespace {

absl::optional<envoy::config::accesslog::v3::AccessLog> testUpstreamLog() {
  // Custom format without timestamps or durations.
  const std::string yaml = R"EOF(
name: accesslog
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
  log_format:
    text_format_source:
      inline_string: "%REQ(:METHOD)% %REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL% %RESPONSE_CODE%
      %RESPONSE_FLAGS% %BYTES_RECEIVED% %BYTES_SENT% %REQ(:AUTHORITY)% %UPSTREAM_HOST%
      %UPSTREAM_LOCAL_ADDRESS% %RESP(X-UPSTREAM-HEADER)% %TRAILER(X-TRAILER)%\n"
  path: "/dev/null"
  )EOF";

  envoy::config::accesslog::v3::AccessLog upstream_log;
  TestUtility::loadFromYaml(yaml, upstream_log);

  return absl::optional<envoy::config::accesslog::v3::AccessLog>(upstream_log);
}

} // namespace

class TestFilter : public Filter {
public:
  using Filter::Filter;

  // Filter
  RetryStatePtr createRetryState(const RetryPolicy&, Http::RequestHeaderMap&,
                                 const Upstream::ClusterInfo&, const VirtualCluster*,
                                 Runtime::Loader&, Random::RandomGenerator&, Event::Dispatcher&,
                                 TimeSource&, Upstream::ResourcePriority) override {
    EXPECT_EQ(nullptr, retry_state_);
    retry_state_ = new NiceMock<MockRetryState>();
    return RetryStatePtr{retry_state_};
  }

  const Network::Connection* downstreamConnection() const override {
    return &downstream_connection_;
  }

  NiceMock<Network::MockConnection> downstream_connection_;
  MockRetryState* retry_state_{};
};

class RouterUpstreamLogTest : public testing::Test {
public:
  void init(absl::optional<envoy::config::accesslog::v3::AccessLog> upstream_log) {
    envoy::extensions::filters::http::router::v3::Router router_proto;
    static const std::string cluster_name = "cluster_0";

    cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
    ON_CALL(*cluster_info_, name()).WillByDefault(ReturnRef(cluster_name));
    ON_CALL(*cluster_info_, observabilityName()).WillByDefault(ReturnRef(cluster_name));
    ON_CALL(callbacks_.stream_info_, upstreamClusterInfo()).WillByDefault(Return(cluster_info_));

    if (upstream_log) {
      ON_CALL(*context_.access_log_manager_.file_, write(_))
          .WillByDefault(
              Invoke([&](absl::string_view data) { output_.push_back(std::string(data)); }));

      envoy::config::accesslog::v3::AccessLog* current_upstream_log =
          router_proto.add_upstream_log();
      current_upstream_log->CopyFrom(upstream_log.value());
    }

    Stats::StatNameManagedStorage prefix("prefix", context_.scope().symbolTable());
    config_ = std::make_shared<FilterConfig>(prefix.statName(), context_,
                                             ShadowWriterPtr(new MockShadowWriter()), router_proto);
    router_ = std::make_shared<TestFilter>(*config_);
    router_->setDecoderFilterCallbacks(callbacks_);
    EXPECT_CALL(callbacks_.dispatcher_, pushTrackedObject(_)).Times(testing::AnyNumber());
    EXPECT_CALL(callbacks_.dispatcher_, popTrackedObject(_)).Times(testing::AnyNumber());

    upstream_locality_.set_zone("to_az");
    context_.cluster_manager_.initializeThreadLocalClusters({"fake_cluster"});
    ON_CALL(*context_.cluster_manager_.thread_local_cluster_.conn_pool_.host_, address())
        .WillByDefault(Return(host_address_));
    ON_CALL(*context_.cluster_manager_.thread_local_cluster_.conn_pool_.host_, locality())
        .WillByDefault(ReturnRef(upstream_locality_));
    router_->downstream_connection_.stream_info_.downstream_address_provider_->setLocalAddress(
        host_address_);
    router_->downstream_connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
        Network::Utility::parseInternetAddressAndPort("1.2.3.4:80"));
  }

  void expectResponseTimerCreate() {
    response_timeout_ = new Event::MockTimer(&callbacks_.dispatcher_);
    EXPECT_CALL(*response_timeout_, enableTimer(_, _));
    EXPECT_CALL(*response_timeout_, disableTimer());
  }

  void expectPerTryTimerCreate() {
    per_try_timeout_ = new Event::MockTimer(&callbacks_.dispatcher_);
    EXPECT_CALL(*per_try_timeout_, enableTimer(_, _));
    EXPECT_CALL(*per_try_timeout_, disableTimer());
  }

  void
  run(uint64_t response_code,
      const std::initializer_list<std::pair<std::string, std::string>>& request_headers_init,
      const std::initializer_list<std::pair<std::string, std::string>>& response_headers_init,
      const std::initializer_list<std::pair<std::string, std::string>>& response_trailers_init) {
    NiceMock<Http::MockRequestEncoder> encoder;
    Http::ResponseDecoder* response_decoder = nullptr;

    EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.conn_pool_, newStream(_, _))
        .WillOnce(Invoke(
            [&](Http::ResponseDecoder& decoder,
                Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
              response_decoder = &decoder;
              EXPECT_CALL(encoder.stream_, connectionLocalAddress())
                  .WillRepeatedly(ReturnRef(upstream_local_address1_));
              callbacks.onPoolReady(
                  encoder, context_.cluster_manager_.thread_local_cluster_.conn_pool_.host_,
                  stream_info_, Http::Protocol::Http10);
              return nullptr;
            }));
    expectResponseTimerCreate();

    Http::TestRequestHeaderMapImpl headers(request_headers_init);
    HttpTestUtility::addDefaultHeaders(headers);
    router_->decodeHeaders(headers, true);

    EXPECT_CALL(*router_->retry_state_, shouldRetryHeaders(_, _)).WillOnce(Return(RetryStatus::No));

    Http::ResponseHeaderMapPtr response_headers(
        new Http::TestResponseHeaderMapImpl(response_headers_init));
    response_headers->setStatus(response_code);

    EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
                putHttpResponseCode(response_code));
    // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
    response_decoder->decodeHeaders(std::move(response_headers), false);

    Http::ResponseTrailerMapPtr response_trailers(
        new Http::TestResponseTrailerMapImpl(response_trailers_init));
    response_decoder->decodeTrailers(std::move(response_trailers));
  }

  void run() { run(200, {}, {}, {}); }

  void runWithRetry() {
    NiceMock<Http::MockRequestEncoder> encoder1;
    Http::ResponseDecoder* response_decoder = nullptr;

    EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.conn_pool_, newStream(_, _))
        .WillOnce(Invoke(
            [&](Http::ResponseDecoder& decoder,
                Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
              response_decoder = &decoder;
              EXPECT_CALL(encoder1.stream_, connectionLocalAddress())
                  .WillRepeatedly(ReturnRef(upstream_local_address1_));
              callbacks.onPoolReady(
                  encoder1, context_.cluster_manager_.thread_local_cluster_.conn_pool_.host_,
                  stream_info_, Http::Protocol::Http10);
              return nullptr;
            }));
    expectPerTryTimerCreate();
    expectResponseTimerCreate();

    Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"},
                                           {"x-envoy-internal", "true"},
                                           {"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
    HttpTestUtility::addDefaultHeaders(headers);
    router_->decodeHeaders(headers, true);

    router_->retry_state_->expectResetRetry();
    EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::LocalOriginTimeout, _));
    per_try_timeout_->invokeCallback();

    // We expect this reset to kick off a new request.
    NiceMock<Http::MockRequestEncoder> encoder2;
    EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.conn_pool_, newStream(_, _))
        .WillOnce(Invoke([&](Http::ResponseDecoder& decoder,
                             Http::ConnectionPool::Callbacks& callbacks)
                             -> Http::ConnectionPool::Cancellable* {
          response_decoder = &decoder;
          EXPECT_CALL(
              context_.cluster_manager_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess, _));
          EXPECT_CALL(encoder2.stream_, connectionLocalAddress())
              .WillRepeatedly(ReturnRef(upstream_local_address2_));
          callbacks.onPoolReady(encoder2,
                                context_.cluster_manager_.thread_local_cluster_.conn_pool_.host_,
                                stream_info_, Http::Protocol::Http10);
          return nullptr;
        }));
    expectPerTryTimerCreate();
    router_->retry_state_->callback_();

    // Normal response.
    EXPECT_CALL(*router_->retry_state_, shouldRetryHeaders(_, _)).WillOnce(Return(RetryStatus::No));
    Http::ResponseHeaderMapPtr response_headers(
        new Http::TestResponseHeaderMapImpl{{":status", "200"}});
    EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
                putHttpResponseCode(200));
    response_decoder->decodeHeaders(std::move(response_headers), true);
  }

  std::vector<std::string> output_;

  NiceMock<Server::Configuration::MockFactoryContext> context_;

  envoy::config::core::v3::Locality upstream_locality_;
  Network::Address::InstanceConstSharedPtr host_address_{
      Network::Utility::resolveUrl("tcp://10.0.0.5:9211")};
  Network::Address::InstanceConstSharedPtr upstream_local_address1_{
      Network::Utility::resolveUrl("tcp://10.0.0.5:10211")};
  Network::Address::InstanceConstSharedPtr upstream_local_address2_{
      Network::Utility::resolveUrl("tcp://10.0.0.5:10212")};
  Event::MockTimer* response_timeout_{};
  Event::MockTimer* per_try_timeout_{};

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  std::shared_ptr<FilterConfig> config_;
  std::shared_ptr<TestFilter> router_;
  std::shared_ptr<NiceMock<Upstream::MockClusterInfo>> cluster_info_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(RouterUpstreamLogTest, NoLogConfigured) {
  init({});
  run();

  EXPECT_TRUE(output_.empty());
}

TEST_F(RouterUpstreamLogTest, LogSingleTry) {
  init(testUpstreamLog());
  run();

  EXPECT_EQ(output_.size(), 1U);
  EXPECT_EQ(output_.front(), "GET / HTTP/1.0 200 - 0 0 host 10.0.0.5:9211 10.0.0.5:10211 - -\n");
}

TEST_F(RouterUpstreamLogTest, LogRetries) {
  init(testUpstreamLog());
  runWithRetry();

  EXPECT_EQ(output_.size(), 2U);
  EXPECT_EQ(output_.front(), "GET / HTTP/1.0 0 UT 0 0 host 10.0.0.5:9211 10.0.0.5:10211 - -\n");
  EXPECT_EQ(output_.back(), "GET / HTTP/1.0 200 - 0 0 host 10.0.0.5:9211 10.0.0.5:10212 - -\n");
}

TEST_F(RouterUpstreamLogTest, LogFailure) {
  init(testUpstreamLog());
  run(503, {}, {}, {});

  EXPECT_EQ(output_.size(), 1U);
  EXPECT_EQ(output_.front(), "GET / HTTP/1.0 503 - 0 0 host 10.0.0.5:9211 10.0.0.5:10211 - -\n");
}

TEST_F(RouterUpstreamLogTest, LogHeaders) {
  init(testUpstreamLog());
  run(200, {{"x-envoy-original-path", "/foo"}}, {{"x-upstream-header", "abcdef"}},
      {{"x-trailer", "value"}});

  EXPECT_EQ(output_.size(), 1U);
  EXPECT_EQ(output_.front(),
            "GET /foo HTTP/1.0 200 - 0 0 host 10.0.0.5:9211 10.0.0.5:10211 abcdef value\n");
}

// Test timestamps and durations are emitted.
TEST_F(RouterUpstreamLogTest, LogTimestampsAndDurations) {
  const std::string yaml = R"EOF(
name: accesslog
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
  log_format:
    text_format_source:
      inline_string: "[%START_TIME%] %REQ(:METHOD)% %REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL%
      %DURATION% %RESPONSE_DURATION% %REQUEST_DURATION%"
  path: "/dev/null"
  )EOF";

  envoy::config::accesslog::v3::AccessLog upstream_log;
  TestUtility::loadFromYaml(yaml, upstream_log);

  init(absl::optional<envoy::config::accesslog::v3::AccessLog>(upstream_log));
  run(200, {{"x-envoy-original-path", "/foo"}}, {}, {});

  EXPECT_EQ(output_.size(), 1U);

  // REQUEST_DURATION is "-" because it represents how long it took to receive the downstream
  // request which is not known to the upstream request.
  std::regex log_regex(
      R"EOF(^\[(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2})\.\d{3}Z\] GET /foo HTTP/1.0 \d+ \d+ -$)EOF");
  std::smatch matches;
  EXPECT_TRUE(std::regex_match(output_.front(), matches, log_regex));

  const absl::Time timestamp = TestUtility::parseTime(matches[1].str(), "%Y-%m-%dT%H:%M:%S");

  std::time_t log_time = absl::ToTimeT(timestamp);
  std::time_t now = std::time(nullptr);

  // Check that timestamp is close enough.
  EXPECT_LE(std::abs(std::difftime(log_time, now)), 300);
}

// Test request headers/response headers/response trailers byte size.
TEST_F(RouterUpstreamLogTest, HeaderByteSize) {
  const std::string yaml = R"EOF(
name: accesslog
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
  log_format:
    text_format_source:
      inline_string: "%REQUEST_HEADERS_BYTES% %RESPONSE_HEADERS_BYTES% %RESPONSE_TRAILERS_BYTES%"
  path: "/dev/null"
  )EOF";

  envoy::config::accesslog::v3::AccessLog upstream_log;
  TestUtility::loadFromYaml(yaml, upstream_log);

  init(absl::optional<envoy::config::accesslog::v3::AccessLog>(upstream_log));
  run(200, {{"request-header-name", "request-header-val"}},
      {{"response-header-name", "response-header-val"}},
      {{"response-trailer-name", "response-trailer-val"}});

  EXPECT_EQ(output_.size(), 1U);
  // Request headers:
  // scheme: http
  // :method: GET
  // :authority: host
  // :path: /
  // x-envoy-expected-rq-timeout-ms: 10
  // request-header-name: request-header-val

  // Response headers:
  // :status: 200
  // response-header-name: response-header-val

  // Response trailers:
  // response-trailer-name: response-trailer-val
  EXPECT_EQ(output_.front(), "110 49 41");
}

// Test UPSTREAM_CLUSTER log formatter.
TEST_F(RouterUpstreamLogTest, UpstreamCluster) {
  const std::string yaml = R"EOF(
name: accesslog
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
  log_format:
    text_format_source:
      inline_string: "%UPSTREAM_CLUSTER%"
  path: "/dev/null"
  )EOF";

  envoy::config::accesslog::v3::AccessLog upstream_log;
  TestUtility::loadFromYaml(yaml, upstream_log);

  init(absl::optional<envoy::config::accesslog::v3::AccessLog>(upstream_log));
  run();

  EXPECT_EQ(output_.size(), 1U);
  EXPECT_EQ(output_.front(), "cluster_0");
}

} // namespace Router
} // namespace Envoy
