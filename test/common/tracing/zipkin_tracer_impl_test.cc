#include "common/http/headers.h"
#include "common/http/header_map_impl.h"
#include "common/http/message_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/runtime/uuid_util.h"
#include "common/tracing/http_tracer_impl.h"
#include "common/tracing/zipkin_tracer_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::Test;

namespace Tracing {

class ZipkinDriverTest : public Test {
public:
  void setup(Json::Object& config, bool init_timer) {
    ON_CALL(cm_, httpAsyncClientForCluster("fake_cluster"))
        .WillByDefault(ReturnRef(cm_.async_client_));

    if (init_timer) {
      timer_ = new NiceMock<Event::MockTimer>(&tls_.dispatcher_);
      EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5000)));
    }

    driver_.reset(new ZipkinDriver(config, cm_, stats_, tls_, runtime_, local_info_));
  }

  void setupValidDriver() {
    EXPECT_CALL(cm_, get("fake_cluster")).WillRepeatedly(Return(&cm_.thread_local_cluster_));
    ON_CALL(*cm_.thread_local_cluster_.cluster_.info_, features())
        .WillByDefault(Return(0)); // No HTTP2 for zipkin upstreams

    std::string valid_config = R"EOF(
      {
       "collector_cluster": "fake_cluster",
       "collector_endpoint": "/api/v1/spans"
       }
    )EOF";
    Json::ObjectPtr loader = Json::Factory::LoadFromString(valid_config);

    setup(*loader, true);
  }

  const std::string operation_name_{"test"};
  Http::TestHeaderMapImpl request_headers_{
      {":authority", "api.lyft.com"}, {":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};
  SystemTime start_time_;
  Http::AccessLog::MockRequestInfo request_info_;

  std::unique_ptr<ZipkinDriver> driver_;
  NiceMock<Event::MockTimer>* timer_;
  Stats::IsolatedStoreImpl stats_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
};

TEST_F(ZipkinDriverTest, InitializeDriver) {
  {
    std::string invalid_config = R"EOF(
      {"fake" : "fake"}
    )EOF";
    Json::ObjectPtr loader = Json::Factory::LoadFromString(invalid_config);

    EXPECT_THROW(setup(*loader, false), EnvoyException);
  }

  {
    std::string empty_config = "{}";
    Json::ObjectPtr loader = Json::Factory::LoadFromString(empty_config);

    EXPECT_THROW(setup(*loader, false), EnvoyException);
  }

  {
    // Valid config but not valid cluster.
    EXPECT_CALL(cm_, get("fake_cluster")).WillOnce(Return(nullptr));

    std::string valid_config = R"EOF(
      {
       "collector_cluster": "fake_cluster",
       "collector_endpoint": "/api/v1/spans"
       }
    )EOF";
    Json::ObjectPtr loader = Json::Factory::LoadFromString(valid_config);

    EXPECT_THROW(setup(*loader, false), EnvoyException);
  }

  {
    // Valid config, but upstream cluster supports only http2.
    EXPECT_CALL(cm_, get("fake_cluster")).WillRepeatedly(Return(&cm_.thread_local_cluster_));
    ON_CALL(*cm_.thread_local_cluster_.cluster_.info_, features())
        .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

    std::string valid_config = R"EOF(
      {
       "collector_cluster": "fake_cluster",
       "collector_endpoint": "/api/v1/spans"
       }
    )EOF";
    Json::ObjectPtr loader = Json::Factory::LoadFromString(valid_config);

    EXPECT_THROW(setup(*loader, false), EnvoyException);
  }

  {
    // valid config, without http2 cluster will work
    EXPECT_CALL(cm_, get("fake_cluster")).WillRepeatedly(Return(&cm_.thread_local_cluster_));
    ON_CALL(*cm_.thread_local_cluster_.cluster_.info_, features()).WillByDefault(Return(0));

    std::string valid_config = R"EOF(
      {
       "collector_cluster": "fake_cluster",
       "collector_endpoint": "/api/v1/spans"
       }
    )EOF";
    Json::ObjectPtr loader = Json::Factory::LoadFromString(valid_config);

    setup(*loader, true);
  }
}

TEST_F(ZipkinDriverTest, FlushSeveralSpans) {
  setupValidDriver();

  Http::MockAsyncClientRequest request(&cm_.async_client_);
  Http::AsyncClient::Callbacks* callback;
  const Optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

  EXPECT_CALL(cm_.async_client_, send_(_, _, timeout))
      .WillOnce(
          Invoke([&](Http::MessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            EXPECT_STREQ("/api/v1/spans", message->headers().Path()->value().c_str());
            EXPECT_STREQ("fake_cluster", message->headers().Host()->value().c_str());
            EXPECT_STREQ("application/json", message->headers().ContentType()->value().c_str());

            return &request;
          }));

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.min_flush_spans", 5))
      .Times(2)
      .WillRepeatedly(Return(2));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.request_timeout", 5000U))
      .WillOnce(Return(5000U));

  SpanPtr first_span = driver_->startSpan(request_headers_, operation_name_, start_time_);
  first_span->finishSpan();

  SpanPtr second_span = driver_->startSpan(request_headers_, operation_name_, start_time_);
  second_span->finishSpan();

  Http::MessagePtr msg(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "202"}}}));

  callback->onSuccess(std::move(msg));

  EXPECT_EQ(2U, stats_.counter("tracing.zipkin.spans_sent").value());
  EXPECT_EQ(1U, stats_.counter("tracing.zipkin.reports_sent").value());
  EXPECT_EQ(0U, stats_.counter("tracing.zipkin.reports_dropped").value());

  callback->onFailure(Http::AsyncClient::FailureReason::Reset);

  EXPECT_EQ(1U, stats_.counter("tracing.zipkin.reports_dropped").value());
}

TEST_F(ZipkinDriverTest, FlushOneSpanReportFailure) {
  setupValidDriver();

  Http::MockAsyncClientRequest request(&cm_.async_client_);
  Http::AsyncClient::Callbacks* callback;
  const Optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

  EXPECT_CALL(cm_.async_client_, send_(_, _, timeout))
      .WillOnce(
          Invoke([&](Http::MessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            EXPECT_STREQ("/api/v1/spans", message->headers().Path()->value().c_str());
            EXPECT_STREQ("fake_cluster", message->headers().Host()->value().c_str());
            EXPECT_STREQ("application/json", message->headers().ContentType()->value().c_str());

            return &request;
          }));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.min_flush_spans", 5))
      .WillOnce(Return(1));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.request_timeout", 5000U))
      .WillOnce(Return(5000U));

  SpanPtr span = driver_->startSpan(request_headers_, operation_name_, start_time_);
  span->finishSpan();

  Http::MessagePtr msg(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "404"}}}));

  // AsyncClient can fail with valid HTTP headers
  callback->onSuccess(std::move(msg));

  EXPECT_EQ(1U, stats_.counter("tracing.zipkin.spans_sent").value());
  EXPECT_EQ(0U, stats_.counter("tracing.zipkin.reports_sent").value());
  EXPECT_EQ(1U, stats_.counter("tracing.zipkin.reports_dropped").value());
}

TEST_F(ZipkinDriverTest, FlushSpansTimer) {
  setupValidDriver();

  const Optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));
  EXPECT_CALL(cm_.async_client_, send_(_, _, timeout));

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.min_flush_spans", 5))
      .WillOnce(Return(5));

  SpanPtr span = driver_->startSpan(request_headers_, operation_name_, start_time_);
  span->finishSpan();

  // Timer should be re-enabled.
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5000)));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.request_timeout", 5000U))
      .WillOnce(Return(5000U));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.flush_interval_ms", 5000U))
      .WillOnce(Return(5000U));

  timer_->callback_();

  EXPECT_EQ(1U, stats_.counter("tracing.zipkin.timer_flushed").value());
  EXPECT_EQ(1U, stats_.counter("tracing.zipkin.spans_sent").value());
}

TEST_F(ZipkinDriverTest, SerializeAndDeserializeContext) {
  setupValidDriver();

  // Supply bogus context, that will be simply ignored.
  const std::string invalid_context = "notvalidcontext";
  request_headers_.insertOtSpanContext().value(invalid_context);
  driver_->startSpan(request_headers_, operation_name_, start_time_);

  std::string injected_ctx = request_headers_.OtSpanContext()->value().c_str();
  EXPECT_FALSE(injected_ctx.empty());

  // Supply empty context.
  request_headers_.removeOtSpanContext();
  SpanPtr span = driver_->startSpan(request_headers_, operation_name_, start_time_);

  injected_ctx = request_headers_.OtSpanContext()->value().c_str();
  EXPECT_FALSE(injected_ctx.empty());

  // Supply parent context, request_headers has properly populated x-ot-span-context.
  SpanPtr span_with_parent = driver_->startSpan(request_headers_, operation_name_, start_time_);
  injected_ctx = request_headers_.OtSpanContext()->value().c_str();
  EXPECT_FALSE(injected_ctx.empty());

  // TODO(fabolive): need more tests for B3 annotations
}

} // Tracing
