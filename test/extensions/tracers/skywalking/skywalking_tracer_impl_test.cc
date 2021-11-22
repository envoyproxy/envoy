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

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    authentication_handle(Http::CustomHeaders::get().Authentication);

class SkyWalkingDriverTest : public testing::Test {
public:
  void SetUp() override {
    ON_CALL(local_info_, clusterName()).WillByDefault(ReturnRef(default_service_name_));
    ON_CALL(local_info_, nodeName()).WillByDefault(ReturnRef(default_instance_name_));

    ON_CALL(context_.server_factory_context_, localInfo()).WillByDefault(ReturnRef(local_info_));
    ON_CALL(context_.transport_socket_factory_context_, localInfo())
        .WillByDefault(ReturnRef(local_info_));
    ON_CALL(context_.transport_socket_factory_context_, mainThreadDispatcher())
        .WillByDefault(ReturnRef(dispatcher_));
    ON_CALL(context_.transport_socket_factory_context_, localInfo())
        .WillByDefault(ReturnRef(local_info_));
    ON_CALL(context_.transport_socket_factory_context_, stats()).WillByDefault(ReturnRef(stats_));
    ON_CALL(context_.transport_socket_factory_context_, initManager())
        .WillByDefault(ReturnRef(init_manager_));
  }

  void setupDriver(const std::string& config) {
    TestUtility::loadFromYaml(config, config_);
    driver_ = std::make_unique<Driver>(config_, context_);
  }

  void expectGrpcClientCreated(int expected_stream_create_times) {
    auto mock_client_factory = std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
    auto mock_client = std::make_unique<NiceMock<Grpc::MockAsyncClient>>();
    mock_stream_ptr_ = std::make_unique<NiceMock<Grpc::MockAsyncStream>>();

    EXPECT_CALL(*mock_client, startRaw(_, _, _, _))
        .Times(expected_stream_create_times)
        .WillRepeatedly(Invoke([&](absl::string_view, absl::string_view,
                                   Grpc::RawAsyncStreamCallbacks& callbacks,
                                   const Http::AsyncClient::StreamOptions&) {
          Http::TestRequestHeaderMapImpl request_headers;
          callbacks.onCreateInitialMetadata(request_headers);
          if (!token_.empty()) {
            EXPECT_EQ(
                request_headers.getInline(authentication_handle.handle())->value().getStringView(),
                token_);
          }
          return mock_stream_ptr_.get();
        }));
    EXPECT_CALL(*mock_client_factory, createUncachedRawAsyncClient())
        .WillOnce(Return(ByMove(std::move(mock_client))));

    auto& factory_context = context_.server_factory_context_;

    EXPECT_CALL(factory_context.cluster_manager_.async_client_manager_,
                factoryForGrpcService(_, _, _))
        .WillOnce(Return(ByMove(std::move(mock_client_factory))));
    EXPECT_CALL(factory_context.thread_local_.dispatcher_, createTimer_(_))
        .WillOnce(Invoke([](Event::TimerCb) { return new NiceMock<Event::MockTimer>(); }));
  }

  void validateSpan(Tracing::SpanPtr span, const std::string& expected_service_name,
                    const std::string& expected_instance_name, bool expected_skip_analysis,
                    bool should_send) {
    EXPECT_NE(nullptr, span.get());
    auto* internal_span = dynamic_cast<Span*>(span.get());
    auto& trace_context = internal_span->tracingContext();

    EXPECT_EQ(expected_service_name, trace_context->service());
    EXPECT_EQ(expected_instance_name, trace_context->serviceInstance());
    EXPECT_EQ(expected_skip_analysis, trace_context->skipAnalysis());

    if (should_send) {
      EXPECT_CALL(*mock_stream_ptr_, sendMessageRaw_(_, _));
      internal_span->finishSpan();
    }
  }

protected:
  const std::string default_service_name_ = "default_service";
  const std::string default_instance_name_ = "default_instance";
  std::string token_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_;
  NiceMock<Envoy::Server::Configuration::MockTracerFactoryContext> context_;
  NiceMock<Envoy::Tracing::MockConfig> mock_tracing_config_;
  Event::SimulatedTimeSystem time_system_;
  std::unique_ptr<NiceMock<Grpc::MockAsyncStream>> mock_stream_ptr_{nullptr};
  envoy::config::trace::v3::SkyWalkingConfig config_;
  DriverPtr driver_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Init::MockManager> init_manager_;
};

TEST_F(SkyWalkingDriverTest, TokenUpdateInvoked) {
  expectGrpcClientCreated(2);
  const std::string yaml_string = R"YAML(
grpc_service:
  envoy_grpc:
    cluster_name: fake_cluster
client_config:
  service_name: "service-a"
  instance_name: "instance-a"
  max_cache_size: 2333
  backend_token_sds_config:
    name: sw-token
    sds_config:
      resource_api_version: V3
      api_config_source:
        api_type: GRPC
        transport_api_version: V3
        grpc_services:
          envoy_grpc:
            cluster_name: xds_cluster
  )YAML";
  setupDriver(yaml_string);

  {
    auto previous_header_value = SkyWalkingTestHelper::createPropagatedSW8HeaderValue(false, "");
    Http::TestRequestHeaderMapImpl request_headers{{"sw8", previous_header_value},
                                                   {":path", "/path"},
                                                   {":method", "GET"},
                                                   {":authority", "test.com"}};
    validateSpan(driver_->startSpan(mock_tracing_config_, request_headers, "TEST_OP",
                                    time_system_.systemTime(), Tracing::Decision{}),
                 "service-a", "instance-a", false, true);
    EXPECT_EQ(1U,
              context_.server_factory_context_.scope_.counter("tracing.skywalking.segments_sent")
                  .value());
  }

  // Token update
  token_ = "token";
  const std::string yaml_client = fmt::format(R"YAML(
name: sw-token
generic_secret:
  secret:
    inline_string: {}
)YAML",
                                              token_);

  envoy::extensions::transport_sockets::tls::v3::Secret typed_secret;
  TestUtility::loadFromYaml(yaml_client, typed_secret);
  const auto decoded_resources_client = TestUtility::decodeResources({typed_secret});

  auto callback =
      context_.transport_socket_factory_context_.cluster_manager_.subscription_factory_.callbacks_;
  callback->onConfigUpdate(decoded_resources_client.refvec_, "");

  {
    auto previous_header_value = SkyWalkingTestHelper::createPropagatedSW8HeaderValue(false, "");
    Http::TestRequestHeaderMapImpl request_headers{{"sw8", previous_header_value},
                                                   {":path", "/path"},
                                                   {":method", "GET"},
                                                   {":authority", "test.com"}};
    validateSpan(driver_->startSpan(mock_tracing_config_, request_headers, "TEST_OP",
                                    time_system_.systemTime(), Tracing::Decision{}),
                 "service-a", "instance-a", false, true);
    EXPECT_EQ(2U,
              context_.server_factory_context_.scope_.counter("tracing.skywalking.segments_sent")
                  .value());
  }
}

TEST_F(SkyWalkingDriverTest, SkyWalkingDriverStartSpanTestWithClientConfig) {
  token_ = "token";
  expectGrpcClientCreated(1);

  const std::string yaml_string = fmt::format(R"YAML(
grpc_service:
  envoy_grpc:
    cluster_name: fake_cluster
client_config:
  backend_token: {}
  service_name: "service-a"
  instance_name: "instance-a"
  max_cache_size: 2333
  )YAML",
                                              token_);
  setupDriver(yaml_string);

  Tracing::Decision decision;
  decision.traced = true;
  auto& factory_context = context_.server_factory_context_;
  ON_CALL(mock_tracing_config_, operationName())
      .WillByDefault(Return(Tracing::OperationName::Ingress));

  {
    auto previous_header_value = SkyWalkingTestHelper::createPropagatedSW8HeaderValue(false, "");
    Http::TestRequestHeaderMapImpl request_headers{{"sw8", previous_header_value},
                                                   {":path", "/path"},
                                                   {":method", "GET"},
                                                   {":authority", "test.com"}};
    validateSpan(driver_->startSpan(mock_tracing_config_, request_headers, "TEST_OP",
                                    time_system_.systemTime(), decision),
                 "service-a", "instance-a", false, true);
    EXPECT_EQ(1U, factory_context.scope_.counter("tracing.skywalking.segments_sent").value());
  }

  {
    // Create new span segment with no previous span context.
    Http::TestRequestHeaderMapImpl new_request_headers{
        {":path", "/path"}, {":method", "GET"}, {":authority", "test.com"}};
    validateSpan(driver_->startSpan(mock_tracing_config_, new_request_headers, "",
                                    time_system_.systemTime(), decision),
                 "service-a", "instance-a", false, true);
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
    validateSpan(driver_->startSpan(mock_tracing_config_, request_headers, "TEST_OP",
                                    time_system_.systemTime(), decision),
                 "service-a", "instance-a", true, true);
    EXPECT_EQ(3U, factory_context.scope_.counter("tracing.skywalking.segments_sent").value());
  }
}

TEST_F(SkyWalkingDriverTest, SkyWalkingDriverStartSpanTestNoClientConfig) {
  const std::string yaml_string = R"YAML(
grpc_service:
  envoy_grpc:
    cluster_name: fake_cluster
  )YAML";

  setupDriver(yaml_string);

  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/path"}, {":method", "GET"}, {":authority", "test.com"}};
  validateSpan(driver_->startSpan(mock_tracing_config_, request_headers, "TEST_OP",
                                  time_system_.systemTime(), Tracing::Decision()),
               default_service_name_, default_instance_name_, true, false);
}

} // namespace
} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
