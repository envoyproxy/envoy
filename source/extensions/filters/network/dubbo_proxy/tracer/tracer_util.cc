#include "source/extensions/filters/network/dubbo_proxy/tracer/tracer_util.h"
#include "source/extensions/filters/network/dubbo_proxy/tracer/skywalking_specialization.h"

#include <memory>

#include "source/common/tracing/http_tracer_impl.h"

#include "source/extensions/request_id/uuid/config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Tracer {

void DubboTracerUtility::prepareStreamInfoTraceReason(
    const Config& config, const std::unique_ptr<Http::RequestHeaderMapImpl>& attachment,
    StreamInfo::StreamInfo& stream_info, Random::RandomGenerator& random_generator,
    Envoy::Runtime::Loader& runtime) {

  auto rid_extension = std::make_unique<Extensions::RequestId::UUIDRequestIDExtension>(
      envoy::extensions::request_id::uuid::v3::UuidRequestIdConfig(), random_generator);
  auto final_reason = Tracing::Reason::NotTraceable;
  rid_extension->set(*attachment, false);
  if (!rid_extension->useRequestIdForTraceSampling()) {
    final_reason = Tracing::Reason::Sampling;
  } else {
    const auto rid_to_integer = rid_extension->toInteger(*attachment);
    if (rid_to_integer.has_value()) {
      const uint64_t result = rid_to_integer.value() % 10000;
      const envoy::type::v3::FractionalPercent* client_sampling =
          &config.tracingConfig()->client_sampling_;
      const envoy::type::v3::FractionalPercent* random_sampling =
          &config.tracingConfig()->random_sampling_;
      const envoy::type::v3::FractionalPercent* overall_sampling =
          &config.tracingConfig()->overall_sampling_;
      final_reason = rid_extension->getTraceReason(*attachment);
      if (Tracing::Reason::NotTraceable == final_reason) {
        if (attachment->ClientTraceId() &&
            runtime.snapshot().featureEnabled("tracing.client_enabled", *client_sampling)) {
          final_reason = Tracing::Reason::ClientForced;
          rid_extension->setTraceReason(*attachment, final_reason);
        } else if (attachment->EnvoyForceTrace()) {
          final_reason = Tracing::Reason::ServiceForced;
          rid_extension->setTraceReason(*attachment, final_reason);
        } else if (runtime.snapshot().featureEnabled("tracing.random_sampling", *random_sampling,
                                                     result)) {
          final_reason = Tracing::Reason::Sampling;
          rid_extension->setTraceReason(*attachment, final_reason);
        }
      }
      if (final_reason != Tracing::Reason::NotTraceable &&
          !runtime.snapshot().featureEnabled("tracing.global_enabled", *overall_sampling, result)) {
        final_reason = Tracing::Reason::NotTraceable;
        rid_extension->setTraceReason(*attachment, final_reason);
      }
    }
  }
  stream_info.setTraceReason(final_reason);
}

Tracing::SpanPtr DubboTracerUtility::createDownstreamSpan(
    const Tracing::HttpTracerSharedPtr tracer, const Config& config,
    const std::unique_ptr<Http::RequestHeaderMapImpl>& attachment, const RpcInvocation& invocation,
    const StreamInfo::StreamInfo& stream_info) {

  const auto tracing_decision = Tracing::HttpTracerUtility::shouldTraceRequest(stream_info);
  attachment->setPath(invocation.serviceName() + "." + invocation.methodName());
  auto span = tracer->startSpan(config, *attachment, stream_info, tracing_decision);
  SkyWalkingSpecialization::setSpanLayerToRPCFramework(span);
  return span;
}

Tracing::SpanPtr DubboTracerUtility::createUpstreamSpan(const Tracing::SpanPtr& downstream_span,
                                                        const Config& config,
                                                        const std::string& cluster_name,
                                                        const SystemTime& system_time) {

  auto span =
      downstream_span->spawnChild(config, "router " + cluster_name + " egress", system_time);
  SkyWalkingSpecialization::setSpanLayerToRPCFramework(span);
  return span;
}

void DubboTracerUtility::updateUpstreamRequestAttachment(
    const Tracing::SpanPtr& upstream_span, const Upstream::HostDescriptionConstSharedPtr& host,
    const RpcInvocationImpl* invocation) {
  auto& attachment = invocation->mutableAttachment();
  auto context = Http::createHeaderMap<Http::RequestHeaderMapImpl>(attachment->headers());
  context->setPath(invocation->serviceName() + "." + invocation->methodName());
  upstream_span->injectContext(*context, host);
  context->forEach([&attachment](absl::string_view key, absl::string_view val) -> bool {
    auto k = std::string(key);
    auto value = std::string(val);
    attachment->remove(k);
    attachment->insert(k, value);
    return true;
  });
}

void DubboTracerUtility::finalizeDownstreamSpan(
    const Tracing::SpanPtr& span, const std::unique_ptr<Http::RequestHeaderMapImpl>& attachment,
    const StreamInfo::StreamInfo& stream_info, const Network::Connection* connection,
    const Tracing::Config& config) {

  if (attachment->RequestId()) {
    span->setTag(Tracing::Tags::get().GuidXRequestId, attachment->getRequestIdValue());
  }

  const auto& remote_address = connection->connectionInfoProvider().directRemoteAddress();
  if (remote_address->type() == Network::Address::Type::Ip) {
    const auto remote_ip = remote_address->ip();
    span->setTag(Tracing::Tags::get().PeerAddress, remote_ip->addressAsString());
  } else {
    span->setTag(Tracing::Tags::get().PeerAddress, remote_address->logicalName());
  }

  if (attachment->ClientTraceId()) {
    span->setTag(Tracing::Tags::get().GuidXClientTraceId, attachment->getClientTraceIdValue());
  }

  Tracing::CustomTagContext ctx{attachment.get(), stream_info};
  const Tracing::CustomTagMap* custom_tag_map = config.customTags();
  if (custom_tag_map) {
    for (const auto& it : *custom_tag_map) {
      it.second->applySpan(*span, ctx);
    }
  }

  span->setTag(Tracing::Tags::get().Component, Tracing::Tags::get().Proxy);

  if (stream_info.upstreamInfo() && stream_info.upstreamInfo()->upstreamHost()) {
    span->setTag(Tracing::Tags::get().UpstreamCluster,
                 stream_info.upstreamInfo()->upstreamHost()->cluster().name());
    span->setTag(Tracing::Tags::get().UpstreamClusterName,
                 stream_info.upstreamInfo()->upstreamHost()->cluster().observabilityName());
  }

  span->finishSpan();
}

void DubboTracerUtility::finalizeUpstreamSpan(const Tracing::SpanPtr& span,
                                              const StreamInfo::StreamInfo&,
                                              const Upstream::HostDescriptionConstSharedPtr host,
                                              const Tracing::Config&) {

  if (host) {
    auto upstream_address = host->address();
    span->setTag(Tracing::Tags::get().UpstreamAddress, upstream_address->asStringView());
    span->setTag(Tracing::Tags::get().PeerAddress, upstream_address->asStringView());
    span->setTag(Tracing::Tags::get().UpstreamCluster, host->cluster().name());
    span->setTag(Tracing::Tags::get().UpstreamClusterName, host->cluster().observabilityName());
  }

  span->setTag(Tracing::Tags::get().Component, Tracing::Tags::get().Proxy);

  span->finishSpan();
}

} // namespace Tracer
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
