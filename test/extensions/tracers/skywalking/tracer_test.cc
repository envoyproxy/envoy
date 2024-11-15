#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/tracers/skywalking/tracer.h"

#include "test/extensions/tracers/skywalking/skywalking_test_helper.h"
#include "test/mocks/common.h"
#include "test/mocks/server/tracer_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/simulated_time_system.h"
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

class TracerTest : public testing::Test {
public:
  void setupTracer(const std::string& yaml_string) {
    EXPECT_CALL(mock_dispatcher_, createTimer_(_)).WillOnce(Invoke([](Event::TimerCb) {
      return new NiceMock<Event::MockTimer>();
    }));

    auto mock_client_factory = std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();

    auto mock_client = std::make_unique<NiceMock<Grpc::MockAsyncClient>>();

    mock_stream_ptr_ = std::make_unique<NiceMock<Grpc::MockAsyncStream>>();

    EXPECT_CALL(*mock_client, startRaw(_, _, _, _)).WillOnce(Return(mock_stream_ptr_.get()));
    EXPECT_CALL(*mock_client_factory, createUncachedRawAsyncClient())
        .WillOnce(Return(ByMove(std::move(mock_client))));

    auto& local_info = context_.server_factory_context_.local_info_;

    ON_CALL(local_info, clusterName()).WillByDefault(ReturnRef(test_string));
    ON_CALL(local_info, nodeName()).WillByDefault(ReturnRef(test_string));

    envoy::config::trace::v3::ClientConfig proto_client_config;
    TestUtility::loadFromYaml(yaml_string, proto_client_config);

    tracer_ = std::make_unique<Tracer>(std::make_unique<TraceSegmentReporter>(
        std::move(mock_client_factory), mock_dispatcher_, mock_random_generator_, tracing_stats_,
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_client_config, max_cache_size, 1024),
        proto_client_config.backend_token()));
  }

protected:
  NiceMock<Envoy::Tracing::MockConfig> mock_tracing_config_;
  NiceMock<Envoy::Server::Configuration::MockTracerFactoryContext> context_;
  NiceMock<Event::MockDispatcher>& mock_dispatcher_ = context_.server_factory_context_.dispatcher_;
  NiceMock<Random::MockRandomGenerator>& mock_random_generator_ =
      context_.server_factory_context_.api_.random_;
  Event::GlobalTimeSystem& mock_time_source_ = context_.server_factory_context_.time_system_;
  NiceMock<Stats::MockIsolatedStatsStore>& mock_scope_ = context_.server_factory_context_.store_;
  std::unique_ptr<NiceMock<Grpc::MockAsyncStream>> mock_stream_ptr_{nullptr};
  std::string test_string = "ABCDEFGHIJKLMN";
  SkyWalkingTracerStatsSharedPtr tracing_stats_{
      std::make_shared<SkyWalkingTracerStats>(SkyWalkingTracerStats{
          SKYWALKING_TRACER_STATS(POOL_COUNTER_PREFIX(mock_scope_, "tracing.skywalking."))})};
  TracerPtr tracer_;
};

// Test that the basic functionality of Tracer is working, including creating Span, using Span to
// create new child Spans.
TEST_F(TracerTest, TracerTestCreateNewSpanWithNoPropagationHeaders) {
  setupTracer("{}");
  EXPECT_CALL(mock_random_generator_, random()).WillRepeatedly(Return(666666));

  // Create a new SegmentContext.
  auto segment_context = SkyWalkingTestHelper::createSegmentContext(true, "CURR", "");

  Envoy::Tracing::SpanPtr org_span =
      tracer_->startSpan("/downstream/path", "HTTP", segment_context);
  Span* span = dynamic_cast<Span*>(org_span.get());

  {
    EXPECT_TRUE(span->spanEntity()->spanType() == skywalking::v3::SpanType::Entry);
    EXPECT_TRUE(span->spanEntity()->spanLayer() == skywalking::v3::SpanLayer::Http);
    EXPECT_EQ("", span->getBaggage("FakeStringAndNothingToDo"));
    span->setOperation("FakeStringAndNothingToDo");
    span->setBaggage("FakeStringAndNothingToDo", "FakeStringAndNothingToDo");
    ASSERT_EQ(span->getTraceId(), segment_context->traceId());
    // This method is unimplemented and a noop.
    ASSERT_EQ(span->getSpanId(), "");
    // Test whether the basic functions of Span are normal.
    EXPECT_FALSE(span->spanEntity()->skipAnalysis());
    span->setSampled(false);
    EXPECT_TRUE(span->spanEntity()->skipAnalysis());

    // The initial operation name is consistent with the 'operation' parameter in the 'startSpan'
    // method call.
    EXPECT_EQ("/downstream/path", span->spanEntity()->operationName());

    // Test whether the tag can be set correctly.
    span->setTag("TestTagKeyA", "TestTagValueA");
    span->setTag("TestTagKeyB", "TestTagValueB");

    // When setting the status code tag, the corresponding tag name will be rewritten as
    // 'status_code'.
    span->setTag(Tracing::Tags::get().HttpStatusCode, "200");

    // When setting the error tag, the spanEntity object will also mark itself as an error.
    span->setTag(Tracing::Tags::get().Error, Tracing::Tags::get().True);
    EXPECT_EQ(true, span->spanEntity()->errorStatus());

    // When setting http url tag, the corresponding tag name will be rewritten as 'url'.
    span->setTag(Tracing::Tags::get().HttpUrl, "http://test.com/test/path");

    // When setting peer address tag, the peer will be set.
    span->setTag(Tracing::Tags::get().PeerAddress, "1.2.3.4:8080");
    EXPECT_EQ("1.2.3.4:8080", span->spanEntity()->peer());

    absl::string_view sample{"GETxx"};
    sample.remove_suffix(2);
    span->setTag(Tracing::Tags::get().HttpMethod, sample);

    span->log(SystemTime{std::chrono::duration<int, std::milli>(100)}, "abc");

    // Evaluate tag values and log values at last.
    auto span_object = span->spanEntity()->createSpanObject();
    EXPECT_EQ("TestTagValueA", span_object.tags().at(0).value());
    EXPECT_EQ("TestTagValueB", span_object.tags().at(1).value());

    EXPECT_EQ("status_code", span_object.tags().at(2).key());
    EXPECT_EQ("200", span_object.tags().at(2).value());

    EXPECT_EQ(Tracing::Tags::get().Error, span_object.tags().at(3).key());
    EXPECT_EQ(Tracing::Tags::get().True, span_object.tags().at(3).value());

    EXPECT_EQ("url", span_object.tags().at(4).key());
    EXPECT_EQ("http://test.com/test/path", span_object.tags().at(4).value());

    EXPECT_EQ("GET", span_object.tags().at(5).value());

    EXPECT_EQ(1, span_object.logs().size());
    EXPECT_LT(0, span_object.logs().at(0).time());
    EXPECT_EQ("abc", span_object.logs().at(0).data().at(0).value());
  }

  {
    Envoy::Tracing::SpanPtr org_first_child_span =
        org_span->spawnChild(mock_tracing_config_, "TestChild", mock_time_source_.systemTime());

    Span* first_child_span = dynamic_cast<Span*>(org_first_child_span.get());

    EXPECT_TRUE(first_child_span->spanEntity()->spanType() == skywalking::v3::SpanType::Exit);

    EXPECT_FALSE(first_child_span->spanEntity()->skipAnalysis());
    EXPECT_EQ(1, first_child_span->spanEntity()->spanId());
    EXPECT_EQ(0, first_child_span->spanEntity()->parentSpanId());

    // "TestChild" will be ignored and operation name of parent span will be used by default for
    // child span (EXIT span).
    EXPECT_EQ(span->spanEntity()->operationName(), first_child_span->spanEntity()->operationName());

    Tracing::TestTraceContextImpl first_child_headers{{":authority", "test.com"},
                                                      {":path", "/upstream/path"}};
    Upstream::HostDescriptionConstSharedPtr host{
        new testing::NiceMock<Upstream::MockHostDescription>()};
    Upstream::ClusterInfoConstSharedPtr cluster{new testing::NiceMock<Upstream::MockClusterInfo>()};
    Tracing::UpstreamContext upstream_context(host.get(), cluster.get(), Tracing::ServiceType::Http,
                                              false);

    first_child_span->injectContext(first_child_headers, upstream_context);
    // Operation name of child span (EXIT span) will be override by the latest path of upstream
    // request.
    EXPECT_EQ("/upstream/path", first_child_span->spanEntity()->operationName());

    auto sp = createSpanContext(std::string(first_child_headers.get("sw8").value()));
    EXPECT_EQ("CURR#SERVICE", sp->service());
    EXPECT_EQ("CURR#INSTANCE", sp->serviceInstance());
    EXPECT_EQ("/downstream/path", sp->endpoint());
    EXPECT_EQ("10.0.0.1:443", sp->targetAddress());

    first_child_span->finishSpan();
    EXPECT_NE(0, first_child_span->spanEntity()->endTime());
  }

  {
    Envoy::Tracing::SpanPtr org_second_child_span =
        org_span->spawnChild(mock_tracing_config_, "TestChild", mock_time_source_.systemTime());

    Span* second_child_span = dynamic_cast<Span*>(org_second_child_span.get());

    EXPECT_TRUE(second_child_span->spanEntity()->spanType() == skywalking::v3::SpanType::Exit);

    EXPECT_FALSE(second_child_span->spanEntity()->skipAnalysis());
    EXPECT_EQ(2, second_child_span->spanEntity()->spanId());
    EXPECT_EQ(0, second_child_span->spanEntity()->parentSpanId());

    // "TestChild" will be ignored and operation name of parent span will be used by default for
    // child span (EXIT span).
    EXPECT_EQ(span->spanEntity()->operationName(),
              second_child_span->spanEntity()->operationName());

    Tracing::TestTraceContextImpl second_child_headers{{":authority", "test.com"}};

    second_child_span->injectContext(second_child_headers, Tracing::UpstreamContext());
    auto sp = createSpanContext(std::string(second_child_headers.get("sw8").value()));
    EXPECT_EQ("CURR#SERVICE", sp->service());
    EXPECT_EQ("CURR#INSTANCE", sp->serviceInstance());
    EXPECT_EQ("/downstream/path", sp->endpoint());
    EXPECT_EQ("test.com", sp->targetAddress());

    second_child_span->finishSpan();
    EXPECT_NE(0, second_child_span->spanEntity()->endTime());
  }

  segment_context->setSkipAnalysis();

  {
    Envoy::Tracing::SpanPtr org_third_child_span =
        org_span->spawnChild(mock_tracing_config_, "TestChild", mock_time_source_.systemTime());
    Span* third_child_span = dynamic_cast<Span*>(org_third_child_span.get());

    // SkipAnalysis is true by default with calling setSkipAnalysis() on segment_context.
    EXPECT_TRUE(third_child_span->spanEntity()->skipAnalysis());
    EXPECT_EQ(3, third_child_span->spanEntity()->spanId());
    EXPECT_EQ(0, third_child_span->spanEntity()->parentSpanId());

    third_child_span->finishSpan();
    EXPECT_NE(0, third_child_span->spanEntity()->endTime());
  }

  // When the child span ends, the data is not reported immediately, but the end time is set.
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.segments_sent").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.segments_dropped").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.cache_flushed").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.segments_flushed").value());

  // When the first span in the current segment ends, the entire segment is reported.
  EXPECT_CALL(*mock_stream_ptr_, sendMessageRaw_(_, _));
  org_span->finishSpan();

  EXPECT_EQ(1U, mock_scope_.counter("tracing.skywalking.segments_sent").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.segments_dropped").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.cache_flushed").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.segments_flushed").value());

  {
    auto rpc_context = SkyWalkingTestHelper::createSegmentContext(true, "CURR", "");
    Envoy::Tracing::SpanPtr org_rpc_span =
        tracer_->startSpan("io.envoyproxy.rpc.demo.Service", "RPCFramework", rpc_context);
    Span* rpc_span = dynamic_cast<Span*>(org_rpc_span.get());
    EXPECT_TRUE(rpc_span->spanEntity()->spanLayer() == skywalking::v3::SpanLayer::RPCFramework);
    EXPECT_EQ(rpc_span->spanEntity()->operationName(), "io.envoyproxy.rpc.demo.Service");
  }

  {
    // When protocol is not known by "skywalking", the span layer will be Unknown
    auto unknown_context = SkyWalkingTestHelper::createSegmentContext(true, "CURR", "");
    Envoy::Tracing::SpanPtr org_unknown_span = tracer_->startSpan(
        "com.company.department.business.Service", "InternalPrivateFramework", unknown_context);
    Span* unknown_span = dynamic_cast<Span*>(org_unknown_span.get());
    EXPECT_TRUE(unknown_span->spanEntity()->spanLayer() == skywalking::v3::SpanLayer::Unknown);
    EXPECT_EQ(unknown_span->spanEntity()->operationName(),
              "com.company.department.business.Service");
  }
}

} // namespace
} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
