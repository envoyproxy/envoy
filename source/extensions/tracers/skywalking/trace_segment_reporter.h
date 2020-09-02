#pragma once

#include "envoy/grpc/async_client_manager.h"

#include "common/Common.pb.h"
#include "common/grpc/async_client_impl.h"

#include "extensions/tracers/skywalking/skywalking_types.h"

#include "language-agent/Tracing.pb.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

class TraceSegmentReporter : Grpc::AsyncStreamCallbacks<Commands> {
public:
  explicit TraceSegmentReporter(Grpc::AsyncClientFactoryPtr&& factory,
                                Event::Dispatcher& dispatcher);

  // Grpc::AsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
  void onReceiveMessage(std::unique_ptr<Commands>&&) override {}
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
  void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override;

  void closeStream();
  void report(const SpanObject& span_object);

private:
  void sendTraceSegment(const SegmentObject& request);
  void establishNewStream();
  void handleFailure();
  void setRetryTimer();

  Grpc::AsyncClient<SegmentObject, Commands> client_;
  Grpc::AsyncStream<SegmentObject> stream_{};
  const Protobuf::MethodDescriptor& service_method_;

  Event::TimerPtr retry_timer_;
};

using TraceSegmentReporterPtr = std::unique_ptr<TraceSegmentReporter>;

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
