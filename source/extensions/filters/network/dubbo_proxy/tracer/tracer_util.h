#pragma once

#include <string>

#include "envoy/common/random_generator.h"
#include "envoy/network/address.h"

#include "source/extensions/filters/network/dubbo_proxy/message_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/filters/filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Tracer {

class DubboTracerUtility {
public:
  static void prepareStreamInfoTraceReason(
      const Config& config, const std::unique_ptr<Http::RequestHeaderMapImpl>& attachment,
      StreamInfo::StreamInfo& stream_info, Random::RandomGenerator& random_generator,
      Envoy::Runtime::Loader& runtime);

  static Tracing::SpanPtr
  createDownstreamSpan(const Tracing::HttpTracerSharedPtr tracer, const Config& config,
                       const std::unique_ptr<Http::RequestHeaderMapImpl>& attachment,
                       const RpcInvocation& invocation, const StreamInfo::StreamInfo& stream_info);

  static Tracing::SpanPtr createUpstreamSpan(const Tracing::SpanPtr& downstream_span,
                                             const Config& config, const std::string& cluster_name,
                                             const SystemTime& system_time);

  static void updateUpstreamRequestAttachment(const Tracing::SpanPtr& upstream_span,
                                              const Upstream::HostDescriptionConstSharedPtr& host,
                                              const RpcInvocationImpl* invocation);

  static void finalizeDownstreamSpan(const Tracing::SpanPtr& span,
                                     const std::unique_ptr<Http::RequestHeaderMapImpl>& attachment,
                                     const StreamInfo::StreamInfo& stream_info,
                                     const Network::Connection* connection,
                                     const Tracing::Config& config);

  static void finalizeUpstreamSpan(const Tracing::SpanPtr& span,
                                   const StreamInfo::StreamInfo& stream_info,
                                   const Upstream::HostDescriptionConstSharedPtr host,
                                   const Tracing::Config& config);
};

} // namespace Tracer
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
