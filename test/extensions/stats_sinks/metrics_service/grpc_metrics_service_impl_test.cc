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
    streamer_ = std::make_unique<GrpcMetricsStreamerImpl>(Grpc::AsyncClientFactoryPtr{factory_},
                                                          local_info_);
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

class TestGrpcMetricsStreamer : public GrpcMetricsStreamer {
public:
  int metric_count;
  // GrpcMetricsStreamer
  void send(envoy::service::metrics::v3::StreamMetricsMessage& message) override {
    metric_count = message.envoy_metrics_size();
  }
};

class MetricsServiceSinkTest : public testing::Test {};

TEST(MetricsServiceSinkTest, CheckSendCall) {
  NiceMock<Stats::MockMetricSnapshot> snapshot;
  Event::SimulatedTimeSystem time_system;
  std::shared_ptr<MockGrpcMetricsStreamer> streamer_{new MockGrpcMetricsStreamer()};

  MetricsServiceSink sink(streamer_, time_system);

  auto counter = std::make_shared<NiceMock<Stats::MockCounter>>();
  counter->name_ = "test_counter";
  counter->latch_ = 1;
  counter->used_ = true;
  snapshot.counters_.push_back({1, *counter});

  auto gauge = std::make_shared<NiceMock<Stats::MockGauge>>();
  gauge->name_ = "test_gauge";
  gauge->value_ = 1;
  gauge->used_ = true;
  snapshot.gauges_.push_back(*gauge);

  auto histogram = std::make_shared<NiceMock<Stats::MockParentHistogram>>();
  histogram->name_ = "test_histogram";
  histogram->used_ = true;

  EXPECT_CALL(*streamer_, send(_));

  sink.flush(snapshot);
}

TEST(MetricsServiceSinkTest, CheckStatsCount) {
  NiceMock<Stats::MockMetricSnapshot> snapshot;
  Event::SimulatedTimeSystem time_system;
  std::shared_ptr<TestGrpcMetricsStreamer> streamer_{new TestGrpcMetricsStreamer()};

  MetricsServiceSink sink(streamer_, time_system);

  auto counter = std::make_shared<NiceMock<Stats::MockCounter>>();
  counter->name_ = "test_counter";
  counter->latch_ = 1;
  counter->used_ = true;
  snapshot.counters_.push_back({1, *counter});

  auto gauge = std::make_shared<NiceMock<Stats::MockGauge>>();
  gauge->name_ = "test_gauge";
  gauge->value_ = 1;
  gauge->used_ = true;
  snapshot.gauges_.push_back(*gauge);

  sink.flush(snapshot);
  EXPECT_EQ(2, (*streamer_).metric_count);

  // Verify only newly added metrics come after endFlush call.
  gauge->used_ = false;
  sink.flush(snapshot);
  EXPECT_EQ(1, (*streamer_).metric_count);
}

} // namespace
} // namespace MetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
