#pragma once

#include <queue>

#include "envoy/config/trace/v3/skywalking.pb.h"
#include "envoy/grpc/async_client_manager.h"

#include "source/common/common/backoff_strategy.h"
#include "source/common/grpc/async_client_impl.h"
#include "source/extensions/tracers/skywalking/skywalking_stats.h"

#include "cpp2sky/tracing_context.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

using cpp2sky::TracingContextPtr;

class TraceSegmentReporter : public Logger::Loggable<Logger::Id::tracing>,
                             public Grpc::AsyncStreamCallbacks<skywalking::v3::Commands> {
public:
  explicit TraceSegmentReporter(Grpc::AsyncClientFactoryPtr&& factory,
                                Event::Dispatcher& dispatcher, Random::RandomGenerator& random,
                                SkyWalkingTracerStatsSharedPtr stats, uint32_t delayed_buffer_size,
                                const std::string& token);
  ~TraceSegmentReporter() override;

  // Grpc::AsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap& metadata) override;
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
  void onReceiveMessage(std::unique_ptr<skywalking::v3::Commands>&&) override {}
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
  void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override;

  void report(TracingContextPtr tracing_context);

private:
  /*
   * Flush all cached segment objects to the back-end tracing service and close the GRPC stream.
   */
  void closeStream();
  void flushTraceSegments();
  void establishNewStream();
  void handleFailure();
  void setRetryTimer();

  SkyWalkingTracerStatsSharedPtr tracing_stats_;
  Grpc::AsyncClient<skywalking::v3::SegmentObject, skywalking::v3::Commands> client_;
  Grpc::AsyncStream<skywalking::v3::SegmentObject> stream_{};
  const Protobuf::MethodDescriptor& service_method_;
  Random::RandomGenerator& random_generator_;
  // If the connection is unavailable when reporting data, the created SegmentObject will be cached
  // in the queue, and when a new connection is established, the cached data will be reported.
  std::queue<skywalking::v3::SegmentObject> delayed_segments_cache_;
  Event::TimerPtr retry_timer_;
  BackOffStrategyPtr backoff_strategy_;
  std::string token_;
  uint32_t delayed_buffer_size_{0};
};

using TraceSegmentReporterPtr = std::unique_ptr<TraceSegmentReporter>;

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
