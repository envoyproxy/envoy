#include "source/common/network/address_impl.h"
#include "source/common/tracing/custom_tag_impl.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/common/tracing/tracer_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Eq;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Tracing {

TEST(TracerUtilityTest, SimpleTest) {
  StreamInfo::MockStreamInfo stream_info;
  NiceMock<Stats::MockStore> stats;

  // Force traced.
  {
    EXPECT_CALL(stream_info, healthCheck()).WillOnce(Return(false));
    EXPECT_CALL(stream_info, traceReason()).WillOnce(Return(Reason::ServiceForced));

    Decision result = TracerUtility::shouldTraceRequest(stream_info);
    EXPECT_EQ(Reason::ServiceForced, result.reason);
    EXPECT_TRUE(result.traced);
  }

  // Sample traced.
  {
    EXPECT_CALL(stream_info, healthCheck()).WillOnce(Return(false));
    EXPECT_CALL(stream_info, traceReason()).WillOnce(Return(Reason::Sampling));

    Decision result = TracerUtility::shouldTraceRequest(stream_info);
    EXPECT_EQ(Reason::Sampling, result.reason);
    EXPECT_TRUE(result.traced);
  }

  // Health Check request.
  {
    EXPECT_CALL(stream_info, healthCheck()).WillOnce(Return(true));

    Decision result = TracerUtility::shouldTraceRequest(stream_info);
    EXPECT_EQ(Reason::HealthCheck, result.reason);
    EXPECT_FALSE(result.traced);
  }

  // Client traced.
  {
    EXPECT_CALL(stream_info, healthCheck()).WillOnce(Return(false));
    EXPECT_CALL(stream_info, traceReason()).WillOnce(Return(Reason::ClientForced));

    Decision result = TracerUtility::shouldTraceRequest(stream_info);
    EXPECT_EQ(Reason::ClientForced, result.reason);
    EXPECT_TRUE(result.traced);
  }

  // No request id.
  {
    EXPECT_CALL(stream_info, healthCheck()).WillOnce(Return(false));
    EXPECT_CALL(stream_info, traceReason()).WillOnce(Return(Reason::NotTraceable));

    Decision result = TracerUtility::shouldTraceRequest(stream_info);
    EXPECT_EQ(Reason::NotTraceable, result.reason);
    EXPECT_FALSE(result.traced);
  }

  // Operation name.
  {
    EXPECT_EQ("ingress", TracerUtility::toString(OperationName::Ingress));
    EXPECT_EQ("egress", TracerUtility::toString(OperationName::Egress));
  }
}

class FinalizerImplTest : public testing::Test {
protected:
  FinalizerImplTest() {
    Upstream::HostDescriptionConstSharedPtr shared_host(host_);
    stream_info.upstreamInfo()->setUpstreamHost(shared_host);
    ON_CALL(stream_info, upstreamClusterInfo())
        .WillByDefault(
            Return(absl::make_optional<Upstream::ClusterInfoConstSharedPtr>(cluster_info_)));
  }
  struct CustomTagCase {
    std::string custom_tag;
    bool set;
    std::string value;
  };

  void expectSetCustomTags(const std::vector<CustomTagCase>& cases) {
    for (const CustomTagCase& cas : cases) {
      envoy::type::tracing::v3::CustomTag custom_tag;
      TestUtility::loadFromYaml(cas.custom_tag, custom_tag);
      config.custom_tags_.emplace(custom_tag.tag(), CustomTagUtility::createCustomTag(custom_tag));
      if (cas.set) {
        EXPECT_CALL(span, setTag(Eq(custom_tag.tag()), Eq(cas.value)));
      } else {
        EXPECT_CALL(span, setTag(Eq(custom_tag.tag()), _)).Times(0);
      }
    }
  }

  NiceMock<MockSpan> span;
  NiceMock<MockConfig> config;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  std::shared_ptr<NiceMock<Upstream::MockClusterInfo>> cluster_info_{
      std::make_shared<NiceMock<Upstream::MockClusterInfo>>()};
  Upstream::MockHostDescription* host_{new NiceMock<Upstream::MockHostDescription>()};
};

TEST_F(FinalizerImplTest, TestAll) {
  TestEnvironment::setEnvVar("E_CC", "c", 1);

  Tracing::TestTraceContextImpl trace_context{{"x-request-id", "id"}, {"x-bb", "b"}};
  trace_context.context_host_ = "test.com";
  trace_context.context_method_ = "method";
  trace_context.context_path_ = "TestService";
  trace_context.context_protocol_ = "test";

  // Set upstream cluster.
  cluster_info_->name_ = "my_upstream_cluster_from_cluster_info";
  cluster_info_->observability_name_ = "my_upstream_cluster_observable_from_cluster_info";

  // Enable verbose logs.
  EXPECT_CALL(config, verbose).Times(2).WillRepeatedly(Return(true));

  // Downstream address.
  const std::string downstream_ip = "10.0.0.100";
  const auto remote_address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance(downstream_ip, 0, nullptr)};
  stream_info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(remote_address);

  // Timestamps of stream.
  const auto start_timestamp =
      SystemTime{std::chrono::duration_cast<SystemTime::duration>(std::chrono::hours{123})};
  EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(start_timestamp));
  const absl::optional<std::chrono::nanoseconds> nanoseconds = std::chrono::nanoseconds{10};
  const MonotonicTime time = MonotonicTime(nanoseconds.value());
  MockTimeSystem time_system;
  EXPECT_CALL(time_system, monotonicTime())
      .Times(AnyNumber())
      .WillRepeatedly(Return(MonotonicTime(std::chrono::nanoseconds(10))));
  auto& timing = stream_info.upstream_info_->upstreamTiming();
  timing.first_upstream_tx_byte_sent_ = time;
  timing.last_upstream_tx_byte_sent_ = time;
  timing.first_upstream_rx_byte_received_ = time;
  timing.last_upstream_rx_byte_received_ = time;
  stream_info.downstream_timing_.onFirstDownstreamTxByteSent(time_system);
  stream_info.downstream_timing_.onLastDownstreamTxByteSent(time_system);
  stream_info.downstream_timing_.onLastDownstreamRxByteReceived(time_system);

  {
    EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
    EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("-")));
    EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamCluster),
                             Eq("my_upstream_cluster_from_cluster_info")));
    EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamClusterName),
                             Eq("my_upstream_cluster_observable_from_cluster_info")));
    EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamAddress), _));
    EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().PeerAddress),
                             Eq("10.0.0.1:443"))); // Upstream address as 'peer.address'

    const auto log_timestamp =
        start_timestamp + std::chrono::duration_cast<SystemTime::duration>(*nanoseconds);
    EXPECT_CALL(span, log(log_timestamp, Tracing::Logs::get().LastDownstreamRxByteReceived));
    EXPECT_CALL(span, log(log_timestamp, Tracing::Logs::get().FirstUpstreamTxByteSent));
    EXPECT_CALL(span, log(log_timestamp, Tracing::Logs::get().LastUpstreamTxByteSent));
    EXPECT_CALL(span, log(log_timestamp, Tracing::Logs::get().FirstUpstreamRxByteReceived));
    EXPECT_CALL(span, log(log_timestamp, Tracing::Logs::get().LastUpstreamRxByteReceived));
    EXPECT_CALL(span, log(log_timestamp, Tracing::Logs::get().FirstDownstreamTxByteSent));
    EXPECT_CALL(span, log(log_timestamp, Tracing::Logs::get().LastDownstreamTxByteSent));

    expectSetCustomTags({
        {"{ tag: aa, literal: { value: a } }", true, "a"},
        {"{ tag: bb-1, request_header: { name: X-Bb, default_value: _b } }", true, "b"},
        {"{ tag: bb-2, request_header: { name: X-Bb-Not-Found, default_value: b2 } }", true, "b2"},
        {"{ tag: bb-3, request_header: { name: X-Bb-Not-Found } }", false, ""},
        {"{ tag: cc-1, environment: { name: E_CC } }", true, "c"},
        {"{ tag: cc-1-a, environment: { name: E_CC, default_value: _c } }", true, "c"},
        {"{ tag: cc-2, environment: { name: E_CC_NOT_FOUND, default_value: c2 } }", true, "c2"},
        {"{ tag: cc-3, environment: { name: E_CC_NOT_FOUND} }", false, ""},
    });

    TracerUtility::finalizeSpan(span, trace_context, stream_info, config, true);
  }

  {
    EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
    EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("-")));
    EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().PeerAddress),
                             remote_address->asString())); // Downstream address as 'peer.address'
    EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamCluster),
                             Eq("my_upstream_cluster_from_cluster_info")));
    EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamClusterName),
                             Eq("my_upstream_cluster_observable_from_cluster_info")));
    EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamAddress), _));

    const auto log_timestamp =
        start_timestamp + std::chrono::duration_cast<SystemTime::duration>(*nanoseconds);
    EXPECT_CALL(span, log(log_timestamp, Tracing::Logs::get().LastDownstreamRxByteReceived));
    EXPECT_CALL(span, log(log_timestamp, Tracing::Logs::get().FirstUpstreamTxByteSent));
    EXPECT_CALL(span, log(log_timestamp, Tracing::Logs::get().LastUpstreamTxByteSent));
    EXPECT_CALL(span, log(log_timestamp, Tracing::Logs::get().FirstUpstreamRxByteReceived));
    EXPECT_CALL(span, log(log_timestamp, Tracing::Logs::get().LastUpstreamRxByteReceived));
    EXPECT_CALL(span, log(log_timestamp, Tracing::Logs::get().FirstDownstreamTxByteSent));
    EXPECT_CALL(span, log(log_timestamp, Tracing::Logs::get().LastDownstreamTxByteSent));

    expectSetCustomTags({
        {"{ tag: aa, literal: { value: a } }", true, "a"},
        {"{ tag: bb-1, request_header: { name: X-Bb, default_value: _b } }", true, "b"},
        {"{ tag: bb-2, request_header: { name: X-Bb-Not-Found, default_value: b2 } }", true, "b2"},
        {"{ tag: bb-3, request_header: { name: X-Bb-Not-Found } }", false, ""},
        {"{ tag: cc-1, environment: { name: E_CC } }", true, "c"},
        {"{ tag: cc-1-a, environment: { name: E_CC, default_value: _c } }", true, "c"},
        {"{ tag: cc-2, environment: { name: E_CC_NOT_FOUND, default_value: c2 } }", true, "c2"},
        {"{ tag: cc-3, environment: { name: E_CC_NOT_FOUND} }", false, ""},
    });

    TracerUtility::finalizeSpan(span, trace_context, stream_info, config, false);
  }
}

TEST(EgressConfigImplTest, EgressConfigImplTest) {
  EgressConfigImpl config_impl;

  EXPECT_EQ(OperationName::Egress, config_impl.operationName());
  EXPECT_EQ(nullptr, config_impl.customTags());
  EXPECT_EQ(false, config_impl.verbose());
  EXPECT_EQ(Tracing::DefaultMaxPathTagLength, config_impl.maxPathTagLength());
}

TEST(NullTracerTest, BasicFunctionality) {
  NullTracer null_tracer;
  MockConfig config;
  StreamInfo::MockStreamInfo stream_info;
  Tracing::TestTraceContextImpl trace_context{};
  Upstream::HostDescriptionConstSharedPtr host{
      new testing::NiceMock<Upstream::MockHostDescription>()};
  Upstream::ClusterInfoConstSharedPtr cluster{new testing::NiceMock<Upstream::MockClusterInfo>()};
  Tracing::UpstreamContext upstream_context(host.get(), cluster.get(), Tracing::ServiceType::Http,
                                            false);

  SpanPtr span_ptr =
      null_tracer.startSpan(config, trace_context, stream_info, {Reason::Sampling, true});
  EXPECT_TRUE(dynamic_cast<NullSpan*>(span_ptr.get()) != nullptr);

  span_ptr->setOperation("foo");
  span_ptr->setTag("foo", "bar");
  span_ptr->setBaggage("key", "value");
  ASSERT_EQ("", span_ptr->getBaggage("baggage_key"));
  ASSERT_EQ(span_ptr->getTraceId(), "");
  ASSERT_EQ(span_ptr->getSpanId(), "");
  span_ptr->injectContext(trace_context, upstream_context);
  span_ptr->log(SystemTime(), "fake_event");

  EXPECT_NE(nullptr, span_ptr->spawnChild(config, "foo", SystemTime()));
}

class TracerImplTest : public testing::Test {
public:
  TracerImplTest() {
    driver_ = new NiceMock<MockDriver>();
    DriverPtr driver_ptr(driver_);
    tracer_ = std::make_shared<TracerImpl>(std::move(driver_ptr), local_info_);
    Upstream::HostDescriptionConstSharedPtr shared_host(host_);
    stream_info_.upstreamInfo()->setUpstreamHost(shared_host);

    ON_CALL(stream_info_, upstreamClusterInfo())
        .WillByDefault(
            Return(absl::make_optional<Upstream::ClusterInfoConstSharedPtr>(cluster_info_)));
  }

  Http::TestRequestHeaderMapImpl request_headers_{
      {":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}, {":authority", "test"}};
  Http::TestResponseHeaderMapImpl response_headers_{{":status", "200"},
                                                    {"content-type", "application/grpc"},
                                                    {"grpc-status", "14"},
                                                    {"grpc-message", "unavailable"}};
  Http::TestResponseTrailerMapImpl response_trailers_;
  Tracing::HttpTraceContext trace_context_{request_headers_};
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<MockConfig> config_;
  NiceMock<MockDriver>* driver_;
  TracerSharedPtr tracer_;
  std::shared_ptr<NiceMock<Upstream::MockClusterInfo>> cluster_info_{
      std::make_shared<NiceMock<Upstream::MockClusterInfo>>()};
  Upstream::MockHostDescription* host_{new NiceMock<Upstream::MockHostDescription>()};
};

TEST_F(TracerImplTest, BasicFunctionalityNullSpan) {
  EXPECT_CALL(config_, operationName()).Times(2);
  const std::string operation_name = "ingress";
  EXPECT_CALL(*driver_, startSpan_(_, _, _, operation_name, _)).WillOnce(Return(nullptr));
  tracer_->startSpan(config_, trace_context_, stream_info_, {Reason::Sampling, true});
}

TEST_F(TracerImplTest, BasicFunctionalityNodeSet) {
  EXPECT_CALL(local_info_, nodeName());
  EXPECT_CALL(config_, operationName()).Times(2).WillRepeatedly(Return(OperationName::Egress));

  NiceMock<MockSpan>* span = new NiceMock<MockSpan>();
  const std::string operation_name = "egress test";
  EXPECT_CALL(*driver_, startSpan_(_, _, _, operation_name, _)).WillOnce(Return(span));
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(*span, setTag(Eq(Tracing::Tags::get().NodeId), Eq("node_name")));

  tracer_->startSpan(config_, trace_context_, stream_info_, {Reason::Sampling, true});
}

TEST_F(TracerImplTest, ChildGrpcUpstreamSpanTest) {
  EXPECT_CALL(local_info_, nodeName());
  EXPECT_CALL(config_, operationName()).Times(2).WillRepeatedly(Return(OperationName::Egress));

  NiceMock<MockSpan>* span = new NiceMock<MockSpan>();
  const std::string operation_name = "egress test";
  EXPECT_CALL(*driver_, startSpan_(_, _, _, operation_name, _)).WillOnce(Return(span));
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(*span, setTag(Eq(Tracing::Tags::get().NodeId), Eq("node_name")));

  auto parent_span =
      tracer_->startSpan(config_, trace_context_, stream_info_, {Reason::Sampling, true});

  NiceMock<MockSpan>* second_span = new NiceMock<MockSpan>();

  EXPECT_CALL(*span, spawnChild_(_, _, _)).WillOnce(Return(second_span));
  auto child_span =
      parent_span->spawnChild(config_, "fake child of egress test", stream_info_.start_time_);

  const std::string expected_ip = "10.0.0.100";
  const auto remote_address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance(expected_ip, 0, nullptr)};

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http2;
  absl::optional<uint32_t> response_code(200);
  const std::string cluster_name = "fake cluster";
  const std::string ob_cluster_name = "ob fake cluster";
  EXPECT_CALL(stream_info_, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info_, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  EXPECT_CALL(*host_, address()).WillOnce(Return(remote_address));
  EXPECT_CALL(*cluster_info_, name()).WillOnce(ReturnRef(cluster_name));
  EXPECT_CALL(*cluster_info_, observabilityName()).WillOnce(ReturnRef(ob_cluster_name));

  EXPECT_CALL(*second_span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(*second_span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/2")));
  EXPECT_CALL(*second_span,
              setTag(Eq(Tracing::Tags::get().UpstreamAddress), Eq(expected_ip + ":0")));
  EXPECT_CALL(*second_span, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq(expected_ip + ":0")));
  EXPECT_CALL(*second_span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("fake cluster")));
  EXPECT_CALL(*second_span,
              setTag(Eq(Tracing::Tags::get().UpstreamClusterName), Eq("ob fake cluster")));
  EXPECT_CALL(*second_span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("200")));
  EXPECT_CALL(*second_span, setTag(Eq(Tracing::Tags::get().GrpcStatusCode), Eq("14")));
  EXPECT_CALL(*second_span, setTag(Eq(Tracing::Tags::get().GrpcMessage), Eq("unavailable")));
  EXPECT_CALL(*second_span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));

  HttpTracerUtility::onUpstreamResponseHeaders(*child_span, &response_headers_);
  HttpTracerUtility::onUpstreamResponseTrailers(*child_span, &response_trailers_);
  HttpTracerUtility::finalizeUpstreamSpan(*child_span, stream_info_, config_);
}

TEST_F(TracerImplTest, MetadataCustomTagReturnsDefaultValue) {
  envoy::type::tracing::v3::CustomTag::Metadata testing_metadata;
  testing_metadata.mutable_metadata_key()->set_key("key");
  *testing_metadata.mutable_default_value() = "default_value";
  MetadataCustomTag tag("testing", testing_metadata);
  StreamInfo::MockStreamInfo testing_info_;
  CustomTagContext context{trace_context_, testing_info_};
  EXPECT_EQ(tag.value(context), "default_value");
}

} // namespace Tracing
} // namespace Envoy
