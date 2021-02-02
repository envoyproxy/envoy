#pragma once

#include <queue>

#include "envoy/config/trace/v3/skywalking.pb.h"
#include "envoy/grpc/async_client_manager.h"

#include "common/Common.pb.h"
#include "common/common/backoff_strategy.h"
#include "common/grpc/async_client_impl.h"

#include "extensions/tracers/skywalking/skywalking_stats.h"

#include "cpp2sky/segment_context.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

using cpp2sky::SegmentContextPtr;

class TraceSegmentReporter : public Logger::Loggable<Logger::Id::tracing>,
                             public Grpc::AsyncStreamCallbacks<Commands> {
public:
  explicit TraceSegmentReporter(Grpc::AsyncClientFactoryPtr&& factory,
                                Event::Dispatcher& dispatcher, Random::RandomGenerator& random,
                                SkyWalkingTracerStats& stats, uint32_t delayed_buffer_size,
                                const std::string& token);
  ~TraceSegmentReporter() override;

  // Grpc::AsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap& metadata) override;
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
  void onReceiveMessage(std::unique_ptr<Commands>&&) override {}
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
  void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override;

  void report(SegmentContextPtr segment_context);

private:
  /*
   * Flush all cached segment objects to the back-end tracing service and close the GRPC stream.
   */
  void closeStream();
  void flushTraceSegments();
  void establishNewStream();
  void handleFailure();
  void setRetryTimer();

  SkyWalkingTracerStats& tracing_stats_;
  Grpc::AsyncClient<SegmentObject, Commands> client_;
  Grpc::AsyncStream<SegmentObject> stream_{};
  const Protobuf::MethodDescriptor& service_method_;
  Random::RandomGenerator& random_generator_;
  // If the connection is unavailable when reporting data, the created SegmentObject will be cached
  // in the queue, and when a new connection is established, the cached data will be reported.
  std::queue<SegmentObject> delayed_segments_cache_;
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
