#include <memory>
#include <string>
#include <utility>

#include "envoy/config/core/v3/http_uri.pb.h"

#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/sampler_config.h"
#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/sampler_config_provider.h"

#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/tracer_factory_context.h"
#include "test/mocks/tracing/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

class SamplerConfigProviderTest : public testing::Test {
public:
  SamplerConfigProviderTest()
      : request_(&tracer_factory_context_.server_factory_context_.cluster_manager_
                      .thread_local_cluster_.async_client_) {

    const std::string yaml_string = R"EOF(
          tenant: "abc12345"
          cluster_id: -1743916452
          http_service:
            http_uri:
              cluster: "cluster_name"
              uri: "https://testhost.com/api/v2/samplingConfiguration"
              timeout: 0.250s
            request_headers_to_add:
            - header:
                key: "authorization"
                value: "Api-Token tokenval"
          root_spans_per_minute: 1000
    )EOF";
    TestUtility::loadFromYaml(yaml_string, proto_config_);

    ON_CALL(tracer_factory_context_.server_factory_context_.cluster_manager_,
            getThreadLocalCluster(_))
        .WillByDefault(Return(&tracer_factory_context_.server_factory_context_.cluster_manager_
                                   .thread_local_cluster_));
    timer_ = new NiceMock<Event::MockTimer>(
        &tracer_factory_context_.server_factory_context_.dispatcher_);
    ON_CALL(tracer_factory_context_.server_factory_context_.dispatcher_, createTimer_(_))
        .WillByDefault(Invoke([this](Event::TimerCb) { return timer_; }));
  }

protected:
  NiceMock<Envoy::Server::Configuration::MockTracerFactoryContext> tracer_factory_context_;
  envoy::extensions::tracers::opentelemetry::samplers::v3::DynatraceSamplerConfig proto_config_;
  NiceMock<Event::MockTimer>* timer_;
  Http::MockAsyncClientRequest request_;
};

MATCHER_P(MessageMatcher, unusedArg, "") {
  return (arg->headers()
              .get(Http::CustomHeaders::get().Authorization)[0]
              ->value()
              .getStringView() == "Api-Token tokenval") &&
         (arg->headers().get(Http::Headers::get().Path)[0]->value().getStringView() ==
          "/api/v2/samplingConfiguration") &&
         (arg->headers().get(Http::Headers::get().Host)[0]->value().getStringView() ==
          "testhost.com") &&
         (arg->headers().get(Http::Headers::get().Method)[0]->value().getStringView() == "GET");
}

// Test that a request is sent if timer fires
TEST_F(SamplerConfigProviderTest, TestRequestIsSent) {
  EXPECT_CALL(tracer_factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                  .async_client_,
              send_(MessageMatcher("unused-arg"), _, _));
  SamplerConfigProviderImpl config_provider(tracer_factory_context_, proto_config_);
  timer_->invokeCallback();
}

// Test that a pending request is canceled
TEST_F(SamplerConfigProviderTest, TestPendingRequestIsCanceled) {
  class TestRequest : public Http::AsyncClient::Request {
  public:
    MOCK_METHOD(void, cancel, ());
  };

  NiceMock<TestRequest> test_request;
  EXPECT_CALL(test_request, cancel());
  ON_CALL(tracer_factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
              .async_client_,
          send_(_, _, _))
      .WillByDefault(Return(&test_request));
  SamplerConfigProviderImpl config_provider(tracer_factory_context_, proto_config_);
  timer_->invokeCallback();
}

// Test receiving http response code 200 and valid json
TEST_F(SamplerConfigProviderTest, TestResponseOkValidJson) {
  SamplerConfigProviderImpl config_provider(tracer_factory_context_, proto_config_);
  timer_->invokeCallback();

  Http::ResponseMessagePtr message(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  message->body().add("{\n \"rootSpansPerMinute\" : 4356 \n }");
  config_provider.onSuccess(request_, std::move(message));
  EXPECT_EQ(config_provider.getSamplerConfig().getRootSpansPerMinute(), 4356);
  EXPECT_TRUE(timer_->enabled());
}

// Test receiving http response code 200 and invalid json
TEST_F(SamplerConfigProviderTest, TestResponseOkInvalidJson) {
  SamplerConfigProviderImpl config_provider(tracer_factory_context_, proto_config_);
  timer_->invokeCallback();

  Http::ResponseMessagePtr message(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  message->body().add("{\n ");
  config_provider.onSuccess(request_, std::move(message));
  EXPECT_EQ(config_provider.getSamplerConfig().getRootSpansPerMinute(),
            SamplerConfig::ROOT_SPANS_PER_MINUTE_DEFAULT);
  EXPECT_TRUE(timer_->enabled());
}

void doTestResponseCode(Http::Code response_code, bool timer_enabled,
                        SamplerConfigProviderImpl& config_provider,
                        Http::MockAsyncClientRequest& request, NiceMock<Event::MockTimer>* timer,
                        int line_num) {
  SCOPED_TRACE(absl::StrCat(__FUNCTION__, " called from line ", line_num));
  Http::ResponseMessagePtr message(new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
      new Http::TestResponseHeaderMapImpl{{":status", std::to_string(enumToInt(response_code))}}}));
  message->body().add("{\n \"rootSpansPerMinute\" : 1000 \n }");
  config_provider.onSuccess(request, std::move(message));
  EXPECT_EQ(timer->enabled(), timer_enabled);
}

// Test that timer is re-enabled depending on the response code
TEST_F(SamplerConfigProviderTest, TestReenableTimer) {
  SamplerConfigProviderImpl config_provider(tracer_factory_context_, proto_config_);
  timer_->invokeCallback();
  doTestResponseCode(Http::Code::Forbidden, false, config_provider, request_, timer_, __LINE__);
  doTestResponseCode(Http::Code::NotFound, false, config_provider, request_, timer_, __LINE__);
  doTestResponseCode(Http::Code::OK, true, config_provider, request_, timer_, __LINE__);
  timer_->invokeCallback();
  doTestResponseCode(Http::Code::TooManyRequests, true, config_provider, request_, timer_,
                     __LINE__);
  timer_->invokeCallback();
  doTestResponseCode(Http::Code::InternalServerError, true, config_provider, request_, timer_,
                     __LINE__);
  timer_->invokeCallback();
  doTestResponseCode(Http::Code::BadGateway, true, config_provider, request_, timer_, __LINE__);
  timer_->invokeCallback();
  doTestResponseCode(Http::Code::ServiceUnavailable, true, config_provider, request_, timer_,
                     __LINE__);
  timer_->invokeCallback();
  doTestResponseCode(Http::Code::GatewayTimeout, true, config_provider, request_, timer_, __LINE__);
  timer_->invokeCallback();
}

// Test receiving http response code != 200
TEST_F(SamplerConfigProviderTest, TestResponseErrorCode) {
  SamplerConfigProviderImpl config_provider(tracer_factory_context_, proto_config_);
  timer_->invokeCallback();

  Http::ResponseMessagePtr message(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "401"}}}));
  message->body().add("{\n \"rootSpansPerMinute\" : 4356 \n }");
  config_provider.onSuccess(request_, std::move(message));
  EXPECT_EQ(config_provider.getSamplerConfig().getRootSpansPerMinute(),
            SamplerConfig::ROOT_SPANS_PER_MINUTE_DEFAULT);
  EXPECT_FALSE(timer_->enabled());
}

// Test sending failed
TEST_F(SamplerConfigProviderTest, TestOnFailure) {
  SamplerConfigProviderImpl config_provider(tracer_factory_context_, proto_config_);
  timer_->invokeCallback();
  config_provider.onFailure(request_, Http::AsyncClient::FailureReason::Reset);
  EXPECT_EQ(config_provider.getSamplerConfig().getRootSpansPerMinute(),
            SamplerConfig::ROOT_SPANS_PER_MINUTE_DEFAULT);
  EXPECT_TRUE(timer_->enabled());
}

// Test calling onBeforeFinalizeUpstreamSpan
TEST_F(SamplerConfigProviderTest, TestOnBeforeFinalizeUpstreamSpan) {
  Tracing::MockSpan child_span_;
  SamplerConfigProviderImpl config_provider(tracer_factory_context_, proto_config_);
  // onBeforeFinalizeUpstreamSpan() is an empty method, nothing to ASSERT, nothing should happen
  config_provider.onBeforeFinalizeUpstreamSpan(child_span_, nullptr);
}

// Test invoking the timer if no cluster can be found
TEST_F(SamplerConfigProviderTest, TestNoCluster) {
  // simulate no configured cluster, return nullptr.
  ON_CALL(tracer_factory_context_.server_factory_context_.cluster_manager_,
          getThreadLocalCluster(_))
      .WillByDefault(Return(nullptr));
  SamplerConfigProviderImpl config_provider(tracer_factory_context_, proto_config_);
  timer_->invokeCallback();
  // nothing to assert, should not crash or throw.
}

// Test that configured value is used
TEST_F(SamplerConfigProviderTest, TestValueConfigured) {
  const std::string yaml_string = R"EOF(
          tenant: "abc12345"
          cluster_id: -1743916452
          http_service:
            http_uri:
              cluster: "cluster_name"
              uri: "https://testhost.com/otlp/v1/traces"
              timeout: 0.250s
          root_spans_per_minute: 3456
    )EOF";

  envoy::extensions::tracers::opentelemetry::samplers::v3::DynatraceSamplerConfig proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  SamplerConfigProviderImpl config_provider(tracer_factory_context_, proto_config);
  EXPECT_EQ(config_provider.getSamplerConfig().getRootSpansPerMinute(), 3456);
}

// Test using a config without a setting for configured root spans
TEST_F(SamplerConfigProviderTest, TestNoValueConfigured) {
  const std::string yaml_string = R"EOF(
          tenant: "abc12345"
          cluster_id: -1743916452
          http_service:
            http_uri:
              cluster: "cluster_name"
              uri: "https://testhost.com/otlp/v1/traces"
              timeout: 500s
    )EOF";

  envoy::extensions::tracers::opentelemetry::samplers::v3::DynatraceSamplerConfig proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  SamplerConfigProviderImpl config_provider(tracer_factory_context_, proto_config);
  EXPECT_EQ(config_provider.getSamplerConfig().getRootSpansPerMinute(),
            SamplerConfig::ROOT_SPANS_PER_MINUTE_DEFAULT);
}

// Test using a config with 0 configured root spans
TEST_F(SamplerConfigProviderTest, TestValueZeroConfigured) {
  const std::string yaml_string = R"EOF(
          tenant: "abc12345"
          cluster_id: -1743916452
          http_service:
            http_uri:
              cluster: "cluster_name"
              uri: "https://testhost.com/otlp/v1/traces"
              timeout: 0.250s
          root_spans_per_minute: 0
    )EOF";

  envoy::extensions::tracers::opentelemetry::samplers::v3::DynatraceSamplerConfig proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  SamplerConfigProviderImpl config_provider(tracer_factory_context_, proto_config);
  EXPECT_EQ(config_provider.getSamplerConfig().getRootSpansPerMinute(),
            SamplerConfig::ROOT_SPANS_PER_MINUTE_DEFAULT);
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
