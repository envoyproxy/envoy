#include "envoy/service/metrics/v3/metrics_service.pb.h"

#include "extensions/stat_sinks/metrics_service/grpc_metrics_service_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/simulated_time_system.h"

using namespace std::chrono_literals;
using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace MetricsService {
namespace {

class GrpcMetricsStreamerImplTest : public testing::Test {
public:
  using MockMetricsStream = Grpc::MockAsyncStream;
  using MetricsServiceCallbacks =
      Grpc::AsyncStreamCallbacks<envoy::service::metrics::v3::StreamMetricsResponse>;

  GrpcMetricsStreamerImplTest() {
    EXPECT_CALL(*factory_, create()).WillOnce(Invoke([this] {
      return Grpc::RawAsyncClientPtr{async_client_};
    }));
    streamer_ = std::make_unique<GrpcMetricsStreamerImpl>(
        Grpc::AsyncClientFactoryPtr{factory_}, local_info_,
        envoy::config::core::v3::ApiVersion::AUTO);
  }

  void expectStreamStart(MockMetricsStream& stream, MetricsServiceCallbacks** callbacks_to_set) {
    EXPECT_CALL(*async_client_, startRaw(_, _, _, _))
        .WillOnce(Invoke([&stream, callbacks_to_set](absl::string_view, absl::string_view,
                                                     Grpc::RawAsyncStreamCallbacks& callbacks,
                                                     const Http::AsyncClient::StreamOptions&) {
          *callbacks_to_set = dynamic_cast<MetricsServiceCallbacks*>(&callbacks);
          return &stream;
        }));
  }

  LocalInfo::MockLocalInfo local_info_;
  Grpc::MockAsyncClient* async_client_{new NiceMock<Grpc::MockAsyncClient>};
  Grpc::MockAsyncClientFactory* factory_{new Grpc::MockAsyncClientFactory};
  std::unique_ptr<GrpcMetricsStreamerImpl> streamer_;
};

// Test basic metrics streaming flow.
TEST_F(GrpcMetricsStreamerImplTest, BasicFlow) {
  InSequence s;

  // Start a stream and send first message.
  MockMetricsStream stream1;
  MetricsServiceCallbacks* callbacks1;
  expectStreamStart(stream1, &callbacks1);
  EXPECT_CALL(local_info_, node());
  EXPECT_CALL(stream1, sendMessageRaw_(_, false));
  envoy::service::metrics::v3::StreamMetricsMessage message_metrics1;
  streamer_->send(message_metrics1);
  // Verify that sending an empty response message doesn't do anything bad.
  callbacks1->onReceiveMessage(
      std::make_unique<envoy::service::metrics::v3::StreamMetricsResponse>());
}

// Test that stream failure is handled correctly.
TEST_F(GrpcMetricsStreamerImplTest, StreamFailure) {
  InSequence s;

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _))
      .WillOnce(
          Invoke([](absl::string_view, absl::string_view, Grpc::RawAsyncStreamCallbacks& callbacks,
                    const Http::AsyncClient::StreamOptions&) {
            callbacks.onRemoteClose(Grpc::Status::Internal, "bad");
            return nullptr;
          }));
  EXPECT_CALL(local_info_, node());
  envoy::service::metrics::v3::StreamMetricsMessage message_metrics1;
  streamer_->send(message_metrics1);
}

class MockGrpcMetricsStreamer : public GrpcMetricsStreamer {
public:
  // GrpcMetricsStreamer
  MOCK_METHOD(void, send, (envoy::service::metrics::v3::StreamMetricsMessage & message));
};

class MetricsServiceSinkTest : public testing::Test {
public:
  MetricsServiceSinkTest() = default;

  NiceMock<Stats::MockMetricSnapshot> snapshot_;
  Event::SimulatedTimeSystem time_system_;
  std::shared_ptr<MockGrpcMetricsStreamer> streamer_{new MockGrpcMetricsStreamer()};
};

TEST_F(MetricsServiceSinkTest, CheckSendCall) {
  MetricsServiceSink sink(streamer_, time_system_, false);

  auto counter = std::make_shared<NiceMock<Stats::MockCounter>>();
  counter->name_ = "test_counter";
  counter->latch_ = 1;
  counter->used_ = true;
  snapshot_.counters_.push_back({1, *counter});

  auto gauge = std::make_shared<NiceMock<Stats::MockGauge>>();
  gauge->name_ = "test_gauge";
  gauge->value_ = 1;
  gauge->used_ = true;
  snapshot_.gauges_.push_back(*gauge);

  auto histogram = std::make_shared<NiceMock<Stats::MockParentHistogram>>();
  histogram->name_ = "test_histogram";
  histogram->used_ = true;

  EXPECT_CALL(*streamer_, send(_));

  sink.flush(snapshot_);
}

TEST_F(MetricsServiceSinkTest, CheckStatsCount) {
  MetricsServiceSink sink(streamer_, time_system_, false);

  auto counter = std::make_shared<NiceMock<Stats::MockCounter>>();
  counter->name_ = "test_counter";
  counter->value_ = 100;
  counter->used_ = true;
  snapshot_.counters_.push_back({1, *counter});

  auto gauge = std::make_shared<NiceMock<Stats::MockGauge>>();
  gauge->name_ = "test_gauge";
  gauge->value_ = 1;
  gauge->used_ = true;
  snapshot_.gauges_.push_back(*gauge);

  EXPECT_CALL(*streamer_, send(_))
      .WillOnce(Invoke([](envoy::service::metrics::v3::StreamMetricsMessage& message) {
        EXPECT_EQ(2, message.envoy_metrics_size());
      }));
  sink.flush(snapshot_);

  // Verify only newly added metrics come after endFlush call.
  gauge->used_ = false;
  EXPECT_CALL(*streamer_, send(_))
      .WillOnce(Invoke([](envoy::service::metrics::v3::StreamMetricsMessage& message) {
        EXPECT_EQ(1, message.envoy_metrics_size());
      }));
  sink.flush(snapshot_);
}

// Test that verifies counters are correctly reported as current value when configured to do so.
TEST_F(MetricsServiceSinkTest, ReportCountersValues) {
  MetricsServiceSink sink(streamer_, time_system_, false);

  auto counter = std::make_shared<NiceMock<Stats::MockCounter>>();
  counter->name_ = "test_counter";
  counter->value_ = 100;
  counter->used_ = true;
  snapshot_.counters_.push_back({1, *counter});

  EXPECT_CALL(*streamer_, send(_))
      .WillOnce(Invoke([](envoy::service::metrics::v3::StreamMetricsMessage& message) {
        EXPECT_EQ(1, message.envoy_metrics_size());
        EXPECT_EQ(100, message.envoy_metrics(0).metric(0).counter().value());
      }));
  sink.flush(snapshot_);
}

// Test that verifies counters are reported as the delta between flushes when configured to do so.
TEST_F(MetricsServiceSinkTest, ReportCountersAsDeltas) {
  MetricsServiceSink sink(streamer_, time_system_, true);

  auto counter = std::make_shared<NiceMock<Stats::MockCounter>>();
  counter->name_ = "test_counter";
  counter->value_ = 100;
  counter->used_ = true;
  snapshot_.counters_.push_back({1, *counter});

  EXPECT_CALL(*streamer_, send(_))
      .WillOnce(Invoke([](envoy::service::metrics::v3::StreamMetricsMessage& message) {
        EXPECT_EQ(1, message.envoy_metrics_size());
        EXPECT_EQ(1, message.envoy_metrics(0).metric(0).counter().value());
      }));
  sink.flush(snapshot_);
}

} // namespace
} // namespace MetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
