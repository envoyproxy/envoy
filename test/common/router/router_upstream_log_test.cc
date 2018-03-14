#include "common/config/filter_json.h"
#include "common/router/router.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/http/common.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Router {
namespace {

absl::optional<envoy::config::filter::accesslog::v2::AccessLog> testUpstreamLog() {
  // Custom format without timestamps or durations.
  const std::string json_string = R"EOF(
  {
    "path": "/dev/null",
    "format": "%REQ(:METHOD)% %REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL% %RESPONSE_CODE% %RESPONSE_FLAGS% %BYTES_RECEIVED% %BYTES_SENT% %REQ(:AUTHORITY)% %UPSTREAM_HOST% %RESP(X-UPSTREAM-HEADER)%\n"
  }
  )EOF";

  auto json_object_ptr = Json::Factory::loadFromString(json_string);

  envoy::config::filter::accesslog::v2::AccessLog upstream_log;
  Envoy::Config::FilterJson::translateAccessLog(*json_object_ptr, upstream_log);

  return absl::optional<envoy::config::filter::accesslog::v2::AccessLog>(upstream_log);
}

} // namespace

class TestFilter : public Filter {
public:
  using Filter::Filter;

  // Filter
  RetryStatePtr createRetryState(const RetryPolicy&, Http::HeaderMap&, const Upstream::ClusterInfo&,
                                 Runtime::Loader&, Runtime::RandomGenerator&, Event::Dispatcher&,
                                 Upstream::ResourcePriority) override {
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
  RouterUpstreamLogTest() {}

  void init(absl::optional<envoy::config::filter::accesslog::v2::AccessLog> upstream_log) {
    envoy::config::filter::http::router::v2::Router router_proto;

    if (upstream_log) {
      ON_CALL(*context_.access_log_manager_.file_, write(_))
          .WillByDefault(Invoke([&](const std::string& data) { output_.push_back(data); }));

      envoy::config::filter::accesslog::v2::AccessLog* current_upstream_log =
          router_proto.add_upstream_log();
      current_upstream_log->CopyFrom(upstream_log.value());
    }

    config_.reset(new FilterConfig("prefix", context_, ShadowWriterPtr(new MockShadowWriter()),
                                   router_proto));
    router_.reset(new TestFilter(*config_));
    router_->setDecoderFilterCallbacks(callbacks_);

    upstream_locality_.set_zone("to_az");

    ON_CALL(*context_.cluster_manager_.conn_pool_.host_, address())
        .WillByDefault(Return(host_address_));
    ON_CALL(*context_.cluster_manager_.conn_pool_.host_, locality())
        .WillByDefault(ReturnRef(upstream_locality_));
    router_->downstream_connection_.local_address_ = host_address_;
    router_->downstream_connection_.remote_address_ =
        Network::Utility::parseInternetAddressAndPort("1.2.3.4:80");
  }

  void expectResponseTimerCreate() {
    response_timeout_ = new Event::MockTimer(&callbacks_.dispatcher_);
    EXPECT_CALL(*response_timeout_, enableTimer(_));
    EXPECT_CALL(*response_timeout_, disableTimer());
  }

  void expectPerTryTimerCreate() {
    per_try_timeout_ = new Event::MockTimer(&callbacks_.dispatcher_);
    EXPECT_CALL(*per_try_timeout_, enableTimer(_));
    EXPECT_CALL(*per_try_timeout_, disableTimer());
  }

  void
  run(uint64_t response_code,
      const std::initializer_list<std::pair<std::string, std::string>>& request_headers_init,
      const std::initializer_list<std::pair<std::string, std::string>>& response_headers_init) {
    NiceMock<Http::MockStreamEncoder> encoder;
    Http::StreamDecoder* response_decoder = nullptr;

    EXPECT_CALL(context_.cluster_manager_.conn_pool_, newStream(_, _))
        .WillOnce(Invoke(
            [&](Http::StreamDecoder& decoder,
                Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
              response_decoder = &decoder;
              callbacks.onPoolReady(encoder, context_.cluster_manager_.conn_pool_.host_);
              return nullptr;
            }));
    expectResponseTimerCreate();

    Http::TestHeaderMapImpl headers(request_headers_init);
    HttpTestUtility::addDefaultHeaders(headers);
    router_->decodeHeaders(headers, true);

    EXPECT_CALL(*router_->retry_state_, shouldRetry(_, _, _)).WillOnce(Return(RetryStatus::No));

    Http::HeaderMapPtr response_headers(new Http::TestHeaderMapImpl(response_headers_init));
    response_headers->insertStatus().value(response_code);

    EXPECT_CALL(context_.cluster_manager_.conn_pool_.host_->outlier_detector_,
                putHttpResponseCode(response_code));
    response_decoder->decodeHeaders(std::move(response_headers), true);
  }

  void run() { run(200, {}, {}); }

  void runWithRetry() {
    NiceMock<Http::MockStreamEncoder> encoder1;
    Http::StreamDecoder* response_decoder = nullptr;
    EXPECT_CALL(context_.cluster_manager_.conn_pool_, newStream(_, _))
        .WillOnce(Invoke(
            [&](Http::StreamDecoder& decoder,
                Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
              response_decoder = &decoder;
              callbacks.onPoolReady(encoder1, context_.cluster_manager_.conn_pool_.host_);
              return nullptr;
            }));
    expectResponseTimerCreate();
    expectPerTryTimerCreate();

    Http::TestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"},
                                    {"x-envoy-internal", "true"},
                                    {"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
    HttpTestUtility::addDefaultHeaders(headers);
    router_->decodeHeaders(headers, true);

    router_->retry_state_->expectRetry();
    EXPECT_CALL(context_.cluster_manager_.conn_pool_.host_->outlier_detector_,
                putHttpResponseCode(504));
    per_try_timeout_->callback_();

    // We expect this reset to kick off a new request.
    NiceMock<Http::MockStreamEncoder> encoder2;
    EXPECT_CALL(context_.cluster_manager_.conn_pool_, newStream(_, _))
        .WillOnce(Invoke(
            [&](Http::StreamDecoder& decoder,
                Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
              response_decoder = &decoder;
              callbacks.onPoolReady(encoder2, context_.cluster_manager_.conn_pool_.host_);
              return nullptr;
            }));
    expectPerTryTimerCreate();
    router_->retry_state_->callback_();

    // Normal response.
    EXPECT_CALL(*router_->retry_state_, shouldRetry(_, _, _)).WillOnce(Return(RetryStatus::No));
    Http::HeaderMapPtr response_headers(new Http::TestHeaderMapImpl{{":status", "200"}});
    EXPECT_CALL(context_.cluster_manager_.conn_pool_.host_->outlier_detector_,
                putHttpResponseCode(200));
    response_decoder->decodeHeaders(std::move(response_headers), true);
  }

  std::vector<std::string> output_;

  NiceMock<Server::Configuration::MockFactoryContext> context_;

  envoy::api::v2::core::Locality upstream_locality_;
  Network::Address::InstanceConstSharedPtr host_address_{
      Network::Utility::resolveUrl("tcp://10.0.0.5:9211")};
  Event::MockTimer* response_timeout_{};
  Event::MockTimer* per_try_timeout_{};

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  std::shared_ptr<FilterConfig> config_;
  std::shared_ptr<TestFilter> router_;
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
  EXPECT_EQ(output_.front(), "GET / HTTP/1.0 200 - 0 0 host 10.0.0.5:9211 -\n");
}

TEST_F(RouterUpstreamLogTest, LogRetries) {
  init(testUpstreamLog());
  runWithRetry();

  EXPECT_EQ(output_.size(), 2U);
  EXPECT_EQ(output_.front(), "GET / HTTP/1.0 0 UT 0 0 host 10.0.0.5:9211 -\n");
  EXPECT_EQ(output_.back(), "GET / HTTP/1.0 200 - 0 0 host 10.0.0.5:9211 -\n");
}

TEST_F(RouterUpstreamLogTest, LogFailure) {
  init(testUpstreamLog());
  run(503, {}, {});

  EXPECT_EQ(output_.size(), 1U);
  EXPECT_EQ(output_.front(), "GET / HTTP/1.0 503 - 0 0 host 10.0.0.5:9211 -\n");
}

TEST_F(RouterUpstreamLogTest, LogHeaders) {
  init(testUpstreamLog());
  run(200, {{"x-envoy-original-path", "/foo"}}, {{"x-upstream-header", "abcdef"}});

  EXPECT_EQ(output_.size(), 1U);
  EXPECT_EQ(output_.front(), "GET /foo HTTP/1.0 200 - 0 0 host 10.0.0.5:9211 abcdef\n");
}

} // namespace Router
} // namespace Envoy
