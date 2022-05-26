#include "source/extensions/tracers/skywalking/skywalking_tracer_impl.h"

#include "test/extensions/tracers/skywalking/skywalking_test_helper.h"
#include "test/mocks/common.h"
#include "test/mocks/server/tracer_factory_context.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {
namespace {

class SkyWalkingDriverTest : public testing::Test {
public:
  void setupSkyWalkingDriver(const std::string& yaml_string) {
    auto mock_client_factory = std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
    auto mock_client = std::make_unique<NiceMock<Grpc::MockAsyncClient>>();
    mock_stream_ptr_ = std::make_unique<NiceMock<Grpc::MockAsyncStream>>();

    EXPECT_CALL(*mock_client, startRaw(_, _, _, _)).WillOnce(Return(mock_stream_ptr_.get()));
    EXPECT_CALL(*mock_client_factory, createUncachedRawAsyncClient())
        .WillOnce(Return(ByMove(std::move(mock_client))));

    auto& factory_context = context_.server_factory_context_;

    EXPECT_CALL(factory_context.cluster_manager_.async_client_manager_,
                factoryForGrpcService(_, _, _))
        .WillOnce(Return(ByMove(std::move(mock_client_factory))));

    EXPECT_CALL(factory_context.thread_local_.dispatcher_, createTimer_(_))
        .WillOnce(Invoke([](Event::TimerCb) { return new NiceMock<Event::MockTimer>(); }));

    ON_CALL(factory_context.local_info_, clusterName()).WillByDefault(ReturnRef(test_string));
    ON_CALL(factory_context.local_info_, nodeName()).WillByDefault(ReturnRef(test_string));

    TestUtility::loadFromYaml(yaml_string, config_);
    driver_ = std::make_unique<Driver>(config_, context_);
  }

protected:
  NiceMock<Envoy::Server::Configuration::MockTracerFactoryContext> context_;
  NiceMock<Envoy::Tracing::MockConfig> mock_tracing_config_;
  Event::SimulatedTimeSystem time_system_;
  std::unique_ptr<NiceMock<Grpc::MockAsyncStream>> mock_stream_ptr_{nullptr};
  envoy::config::trace::v3::SkyWalkingConfig config_;
  std::string test_string = "ABCDEFGHIJKLMN";
  DriverPtr driver_;
};

TEST_F(SkyWalkingDriverTest, SkyWalkingDriverStartSpanTestWithClientConfig) {
  const std::string yaml_string = R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: fake_cluster
  client_config:
    backend_token: "FAKE_FAKE_FAKE_FAKE_FAKE_FAKE"
    service_name: "FAKE_FAKE_FAKE"
    instance_name: "FAKE_FAKE_FAKE_INSTANCE"
    max_cache_size: 2333
  )EOF";
  setupSkyWalkingDriver(yaml_string);

  Tracing::Decision decision;
  decision.reason = Tracing::Reason::Sampling;
  decision.traced = true;
  auto& factory_context = context_.server_factory_context_;

  {
    auto previous_header_value = SkyWalkingTestHelper::createPropagatedSW8HeaderValue(false, "");
    Http::TestRequestHeaderMapImpl request_headers{{"sw8", previous_header_value},
                                                   {":path", "/path"},
                                                   {":method", "GET"},
                                                   {":authority", "test.com"}};
    ON_CALL(mock_tracing_config_, operationName())
        .WillByDefault(Return(Tracing::OperationName::Ingress));

    Tracing::SpanPtr org_span = driver_->startSpan(mock_tracing_config_, request_headers, "TEST_OP",
                                                   time_system_.systemTime(), decision);
    EXPECT_NE(nullptr, org_span.get());

    Span* span = dynamic_cast<Span*>(org_span.get());
    ASSERT(span);

    // "TEST_OP" will be ignored and path of downstream request will be used as the operation name
    // of ENTRY span.
    EXPECT_EQ("/path", span->spanEntity()->operationName());

    EXPECT_EQ("FAKE_FAKE_FAKE", span->tracingContext()->service());
    EXPECT_EQ("FAKE_FAKE_FAKE_INSTANCE", span->tracingContext()->serviceInstance());

    // Tracing decision will be overwrite by skip analysis flag in propagation headers.
    EXPECT_FALSE(span->tracingContext()->skipAnalysis());

    // Since the sampling flag is false, no segment data is reported.
    EXPECT_CALL(*mock_stream_ptr_, sendMessageRaw_(_, _));
    span->finishSpan();

    EXPECT_EQ(1U, factory_context.scope_.counter("tracing.skywalking.segments_sent").value());
  }

  {
    // Create new span segment with no previous span context.
    Http::TestRequestHeaderMapImpl new_request_headers{
        {":path", "/path"}, {":method", "GET"}, {":authority", "test.com"}};

    Tracing::SpanPtr org_span = driver_->startSpan(mock_tracing_config_, new_request_headers, "",
                                                   time_system_.systemTime(), decision);

    Span* span = dynamic_cast<Span*>(org_span.get());
    ASSERT(span);

    // Path of downstream request will be used as the operation name of ENTRY span.
    EXPECT_EQ("/path", span->spanEntity()->operationName());

    EXPECT_FALSE(span->tracingContext()->skipAnalysis());

    EXPECT_CALL(*mock_stream_ptr_, sendMessageRaw_(_, _));
    span->finishSpan();

    EXPECT_EQ(2U, factory_context.scope_.counter("tracing.skywalking.segments_sent").value());
  }

  {
    // Create new span segment with error propagation header.
    Http::TestRequestHeaderMapImpl error_request_headers{
        {":path", "/path"},
        {":method", "GET"},
        {":authority", "test.com"},
        {"sw8", "xxxxxx-error-propagation-header"}};
    Tracing::SpanPtr org_null_span =
        driver_->startSpan(mock_tracing_config_, error_request_headers, "TEST_OP",
                           time_system_.systemTime(), decision);

    EXPECT_EQ(nullptr, dynamic_cast<Span*>(org_null_span.get()));

    auto& null_span = *org_null_span;
    EXPECT_EQ(typeid(null_span).name(), typeid(Tracing::NullSpan).name());
  }

  {
    // Create root segment span with disabled tracing.
    decision.traced = false;
    Http::TestRequestHeaderMapImpl request_headers{
        {":path", "/path"}, {":method", "GET"}, {":authority", "test.com"}};
    Tracing::SpanPtr org_null_span = driver_->startSpan(
        mock_tracing_config_, request_headers, "TEST_OP", time_system_.systemTime(), decision);

    EXPECT_EQ(nullptr, dynamic_cast<Span*>(org_null_span.get()));

    auto& null_span = *org_null_span;
    EXPECT_EQ(typeid(null_span).name(), typeid(Tracing::NullSpan).name());
  }
}

TEST_F(SkyWalkingDriverTest, SkyWalkingDriverStartSpanTestNoClientConfig) {
  const std::string yaml_string = R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: fake_cluster
  )EOF";

  setupSkyWalkingDriver(yaml_string);

  Tracing::Decision decision;
  decision.reason = Tracing::Reason::Sampling;
  decision.traced = true;

  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/path"}, {":method", "GET"}, {":authority", "test.com"}};

  Tracing::SpanPtr org_span = driver_->startSpan(mock_tracing_config_, request_headers, "TEST_OP",
                                                 time_system_.systemTime(), decision);
  EXPECT_NE(nullptr, org_span.get());

  Span* span = dynamic_cast<Span*>(org_span.get());
  ASSERT(span);

  EXPECT_EQ(test_string, span->tracingContext()->service());
  EXPECT_EQ(test_string, span->tracingContext()->serviceInstance());
}

} // namespace
} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
