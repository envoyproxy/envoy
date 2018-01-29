#include "common/stats/grpc_metrics_service_impl.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"

using namespace std::chrono_literals;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Stats {
namespace Metrics {

class GrpcMetricsStreamerImplTest : public testing::Test {
public:
  typedef Grpc::MockAsyncStream MockMetricsStream;
  typedef Grpc::TypedAsyncStreamCallbacks<envoy::api::v2::StreamMetricsResponse>
      MetricsServiceCallbacks;

  GrpcMetricsStreamerImplTest() {
    EXPECT_CALL(*factory_, create()).WillOnce(Invoke([this] {
      return Grpc::AsyncClientPtr{async_client_};
    }));
    streamer_ = std::make_unique<GrpcMetricsStreamerImpl>(Grpc::AsyncClientFactoryPtr{factory_},
                                                          tls_, local_info_);
  }

  void expectStreamStart(MockMetricsStream& stream, MetricsServiceCallbacks** callbacks_to_set) {
    EXPECT_CALL(*async_client_, start(_, _))
        .WillOnce(Invoke([&stream, callbacks_to_set](const Protobuf::MethodDescriptor&,
                                                     Grpc::AsyncStreamCallbacks& callbacks) {
          *callbacks_to_set = dynamic_cast<MetricsServiceCallbacks*>(&callbacks);
          return &stream;
        }));
  }

  NiceMock<ThreadLocal::MockInstance> tls_;
  LocalInfo::MockLocalInfo local_info_;
  Grpc::MockAsyncClient* async_client_{new Grpc::MockAsyncClient};
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
  EXPECT_CALL(stream1, sendMessage(_, false));
  envoy::api::v2::StreamMetricsMessage message_metrics1;
  streamer_->send(message_metrics1);
  // Verify that sending an empty response message doesn't do anything bad.
  callbacks1->onReceiveMessage(std::make_unique<envoy::api::v2::StreamMetricsResponse>());
}

// Test that stream failure is handled correctly.
TEST_F(GrpcMetricsStreamerImplTest, StreamFailure) {
  InSequence s;

  EXPECT_CALL(*async_client_, start(_, _))
      .WillOnce(
          Invoke([](const Protobuf::MethodDescriptor&, Grpc::AsyncStreamCallbacks& callbacks) {
            callbacks.onRemoteClose(Grpc::Status::Internal, "bad");
            return nullptr;
          }));
  EXPECT_CALL(local_info_, node());
  envoy::api::v2::StreamMetricsMessage message_metrics1;
  streamer_->send(message_metrics1);
}

class MockGrpcMetricsStreamer : public GrpcMetricsStreamer {
public:
  // GrpcMetricsStreamer
  MOCK_METHOD1(send, void(envoy::api::v2::StreamMetricsMessage& message));
};

class TestGrpcMetricsStreamer : public GrpcMetricsStreamer {
public:
  int metric_count;
  // GrpcMetricsStreamer
  void send(envoy::api::v2::StreamMetricsMessage& message) {
    metric_count = message.envoy_metrics_size();
  }
};

class MetricsServiceSinkTest : public testing::Test {};

TEST(MetricsServiceSinkTest, CheckSendCall) {
  std::shared_ptr<MockGrpcMetricsStreamer> streamer_{new MockGrpcMetricsStreamer()};

  MetricsServiceSink sink(streamer_);

  sink.beginFlush();

  NiceMock<MockCounter> counter;
  counter.name_ = "test_counter";
  sink.flushCounter(counter, 1);

  NiceMock<MockGauge> gauge;
  gauge.name_ = "test_gauge";
  sink.flushGauge(gauge, 1);
  EXPECT_CALL(*streamer_, send(_));

  sink.endFlush();
}

TEST(MetricsServiceSinkTest, CheckStatsCount) {
  std::shared_ptr<TestGrpcMetricsStreamer> streamer_{new TestGrpcMetricsStreamer()};

  MetricsServiceSink sink(streamer_);

  sink.beginFlush();

  NiceMock<MockCounter> counter;
  counter.name_ = "test_counter";
  sink.flushCounter(counter, 1);

  NiceMock<MockGauge> gauge;
  gauge.name_ = "test_gauge";
  sink.flushGauge(gauge, 1);

  sink.endFlush();
  EXPECT_EQ(2, (*streamer_).metric_count);

  // Verify only newly added metrics come after endFlush call.
  sink.beginFlush();

  NiceMock<MockCounter> counter1;
  counter1.name_ = "test_counter";
  sink.flushCounter(counter1, 1);

  sink.endFlush();
  EXPECT_EQ(1, (*streamer_).metric_count);
}

} // namespace Metrics
} // namespace Stats
} // namespace Envoy
