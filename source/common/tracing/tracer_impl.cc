#include "source/common/tracing/tracer_impl.h"

#include "envoy/upstream/upstream.h"

#include "source/common/stream_info/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Tracing {

namespace {

static void annotateVerbose(Span& span, const StreamInfo::StreamInfo& stream_info) {
  const auto start_time = stream_info.startTime();
  StreamInfo::TimingUtility timing(stream_info);
  if (timing.lastDownstreamRxByteReceived()) {
    span.log(start_time + std::chrono::duration_cast<SystemTime::duration>(
                              *timing.lastDownstreamRxByteReceived()),
             Tracing::Logs::get().LastDownstreamRxByteReceived);
  }
  if (timing.firstUpstreamTxByteSent()) {
    span.log(start_time + std::chrono::duration_cast<SystemTime::duration>(
                              *timing.firstUpstreamTxByteSent()),
             Tracing::Logs::get().FirstUpstreamTxByteSent);
  }
  if (timing.lastUpstreamTxByteSent()) {
    span.log(start_time +
                 std::chrono::duration_cast<SystemTime::duration>(*timing.lastUpstreamTxByteSent()),
             Tracing::Logs::get().LastUpstreamTxByteSent);
  }
  if (timing.firstUpstreamRxByteReceived()) {
    span.log(start_time + std::chrono::duration_cast<SystemTime::duration>(
                              *timing.firstUpstreamRxByteReceived()),
             Tracing::Logs::get().FirstUpstreamRxByteReceived);
  }
  if (timing.lastUpstreamRxByteReceived()) {
    span.log(start_time + std::chrono::duration_cast<SystemTime::duration>(
                              *timing.lastUpstreamRxByteReceived()),
             Tracing::Logs::get().LastUpstreamRxByteReceived);
  }
  if (timing.firstDownstreamTxByteSent()) {
    span.log(start_time + std::chrono::duration_cast<SystemTime::duration>(
                              *timing.firstDownstreamTxByteSent()),
             Tracing::Logs::get().FirstDownstreamTxByteSent);
  }
  if (timing.lastDownstreamTxByteSent()) {
    span.log(start_time + std::chrono::duration_cast<SystemTime::duration>(
                              *timing.lastDownstreamTxByteSent()),
             Tracing::Logs::get().LastDownstreamTxByteSent);
  }
}

} // namespace

const std::string TracerUtility::IngressOperation = "ingress";
const std::string TracerUtility::EgressOperation = "egress";

const std::string& TracerUtility::toString(OperationName operation_name) {
  switch (operation_name) {
  case OperationName::Ingress:
    return IngressOperation;
  case OperationName::Egress:
    return EgressOperation;
  }

  return EMPTY_STRING; // Make the compiler happy.
}

Decision TracerUtility::shouldTraceRequest(const StreamInfo::StreamInfo& stream_info) {
  // Exclude health check requests immediately.
  if (stream_info.healthCheck()) {
    return {Reason::HealthCheck, false};
  }

  const Tracing::Reason trace_reason = stream_info.traceReason();
  switch (trace_reason) {
  case Reason::ClientForced:
  case Reason::ServiceForced:
  case Reason::Sampling:
    return {trace_reason, true};
  default:
    return {trace_reason, false};
  }
}

void TracerUtility::finalizeSpan(Span& span, const TraceContext& trace_context,
                                 const StreamInfo::StreamInfo& stream_info,
                                 const Config& tracing_config, bool upstream_span) {
  span.setTag(Tracing::Tags::get().Component, Tracing::Tags::get().Proxy);

  // Response flag.
  span.setTag(Tracing::Tags::get().ResponseFlags,
              StreamInfo::ResponseFlagUtils::toShortString(stream_info));

  // Downstream info.
  if (!upstream_span) {
    const auto& remote_address = stream_info.downstreamAddressProvider().directRemoteAddress();
    span.setTag(Tracing::Tags::get().PeerAddress, remote_address->asStringView());
  }

  // Cluster info.
  if (auto cluster_info = stream_info.upstreamClusterInfo();
      cluster_info.has_value() && cluster_info.value() != nullptr) {
    span.setTag(Tracing::Tags::get().UpstreamCluster, cluster_info.value()->name());
    span.setTag(Tracing::Tags::get().UpstreamClusterName,
                cluster_info.value()->observabilityName());
  }

  // Upstream info.
  if (stream_info.upstreamInfo() && stream_info.upstreamInfo()->upstreamHost()) {
    auto upstream_address = stream_info.upstreamInfo()->upstreamHost()->address();

    span.setTag(Tracing::Tags::get().UpstreamAddress, upstream_address->asStringView());

    // Upstream address would be 'peer.address' in the case of an upstream span.
    if (upstream_span) {
      span.setTag(Tracing::Tags::get().PeerAddress, upstream_address->asStringView());
    }
  }

  // Verbose timing log.
  if (tracing_config.verbose()) {
    annotateVerbose(span, stream_info);
  }

  // Custom tag from configuration.
  CustomTagContext ctx{trace_context, stream_info};
  if (const CustomTagMap* custom_tag_map = tracing_config.customTags(); custom_tag_map) {
    for (const auto& it : *custom_tag_map) {
      it.second->applySpan(span, ctx);
    }
  }

  // Finish the span.
  span.finishSpan();
}

TracerImpl::TracerImpl(DriverSharedPtr driver, const LocalInfo::LocalInfo& local_info)
    : driver_(std::move(driver)), local_info_(local_info) {}

SpanPtr TracerImpl::startSpan(const Config& config, TraceContext& trace_context,
                              const StreamInfo::StreamInfo& stream_info,
                              const Tracing::Decision tracing_decision) {
  std::string span_name = TracerUtility::toString(config.operationName());

  if (config.operationName() == OperationName::Egress) {
    span_name.append(" ");
    span_name.append(std::string(trace_context.host()));
  }

  SpanPtr active_span =
      driver_->startSpan(config, trace_context, stream_info, span_name, tracing_decision);

  // Set tags related to the local environment
  if (active_span) {
    active_span->setTag(Tracing::Tags::get().NodeId, local_info_.nodeName());
    active_span->setTag(Tracing::Tags::get().Zone, local_info_.zoneName());
  }

  return active_span;
}

} // namespace Tracing
} // namespace Envoy
