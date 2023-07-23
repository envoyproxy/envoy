#include "source/extensions/stat_sinks/metrics_service/grpc_metrics_service_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/simulated_time_system.h"

#include "library/common/extensions/stat_sinks/metrics_service/mobile_grpc_streamer.h"
#include "library/common/extensions/stat_sinks/metrics_service/service.pb.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace EnvoyMobileMetricsService {
namespace {

class EnvoyMobileGrpcMetricsStreamerImplTest : public testing::Test {
public:
  using MockMetricsStream = Grpc::MockAsyncStream;
  using MetricsServiceCallbacks = Grpc::AsyncStreamCallbacks<
      envoymobile::extensions::stat_sinks::metrics_service::EnvoyMobileStreamMetricsResponse>;

  EnvoyMobileGrpcMetricsStreamerImplTest() {
    EXPECT_CALL(*factory_, create()).WillOnce(Invoke([this] {
      return Grpc::RawAsyncClientPtr{async_client_};
    }));

    streamer_ = std::make_unique<EnvoyMobileGrpcMetricsStreamerImpl>(
        Grpc::AsyncClientFactoryPtr{factory_}, local_info_, random_generator);
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

  NiceMock<Random::MockRandomGenerator> random_generator;
  LocalInfo::MockLocalInfo local_info_;
  Grpc::MockAsyncClient* async_client_{new NiceMock<Grpc::MockAsyncClient>};
  Grpc::MockAsyncClientFactory* factory_{new Grpc::MockAsyncClientFactory};
  EnvoyMobileGrpcMetricsStreamerImplPtr streamer_;
};

// Test basic metrics streaming flow.
TEST_F(EnvoyMobileGrpcMetricsStreamerImplTest, BasicFlow) {
  InSequence s;

  // Start a stream and send first message.

  MockMetricsStream stream1;
  MetricsServiceCallbacks* callbacks1;
  expectStreamStart(stream1, &callbacks1);
  EXPECT_CALL(local_info_, node());
  EXPECT_CALL(stream1, sendMessageRaw_(_, false));

  auto metrics =
      std::make_unique<Envoy::Protobuf::RepeatedPtrField<io::prometheus::client::MetricFamily>>();

  streamer_->send(std::move(metrics));
  // Verify that sending an empty response message doesn't do anything bad.
  callbacks1->onReceiveMessage(
      std::make_unique<envoymobile::extensions::stat_sinks::metrics_service::
                           EnvoyMobileStreamMetricsResponse>());
}

// Test response is received.
TEST_F(EnvoyMobileGrpcMetricsStreamerImplTest, WithResponse) {
  InSequence s;

  // Start a stream and send first message.
  MockMetricsStream stream1;
  MetricsServiceCallbacks* callbacks1;
  expectStreamStart(stream1, &callbacks1);
  EXPECT_CALL(local_info_, node());
  EXPECT_CALL(stream1, sendMessageRaw_(_, false));

  auto metrics =
      std::make_unique<Envoy::Protobuf::RepeatedPtrField<io::prometheus::client::MetricFamily>>();

  streamer_->send(std::move(metrics));

  auto response = std::make_unique<
      envoymobile::extensions::stat_sinks::metrics_service::EnvoyMobileStreamMetricsResponse>();
  response->set_batch_id("mock-batch-id");

  // Verify that batch_id of the response message is logged.
  EXPECT_LOG_CONTAINS("debug", "EnvoyMobile streamer received batch_id: mock-batch-id",
                      callbacks1->onReceiveMessage(std::move(response)));
}

// Test that stream failure is handled correctly.
TEST_F(EnvoyMobileGrpcMetricsStreamerImplTest, StreamFailure) {
  InSequence s;

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _))
      .WillOnce(
          Invoke([](absl::string_view, absl::string_view, Grpc::RawAsyncStreamCallbacks& callbacks,
                    const Http::AsyncClient::StreamOptions&) {
            callbacks.onRemoteClose(Grpc::Status::Internal, "bad");
            return nullptr;
          }));
  EXPECT_CALL(local_info_, node());
  auto metrics =
      std::make_unique<Envoy::Protobuf::RepeatedPtrField<io::prometheus::client::MetricFamily>>();
  streamer_->send(std::move(metrics));
}

class MockGrpcMetricsStreamer
    : MetricsService::GrpcMetricsStreamer<
          envoymobile::extensions::stat_sinks::metrics_service::EnvoyMobileStreamMetricsMessage,
          envoymobile::extensions::stat_sinks::metrics_service::EnvoyMobileStreamMetricsResponse> {
public:
  // GrpcMetricsStreamer
  MOCK_METHOD(void, send, (MetricsService::MetricsPtr && metrics));
  MOCK_METHOD(void, onReceiveMessage,
              (std::unique_ptr<envoymobile::extensions::stat_sinks::metrics_service::
                                   EnvoyMobileStreamMetricsResponse> &&
               response));
};

} // namespace
} // namespace EnvoyMobileMetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
