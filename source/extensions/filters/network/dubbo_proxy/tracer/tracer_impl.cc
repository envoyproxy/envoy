#include "source/extensions/filters/network/dubbo_proxy/tracer/tracer_impl.h"

#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/thread_local_cluster.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/message_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Tracer {

void Tracer::onDestroy() {}

void Tracer::setDecoderFilterCallbacks(DubboFilters::DecoderFilterCallbacks&) {
}

FilterStatus Tracer::onMessageDecoded(MessageMetadataSharedPtr, ContextSharedPtr) {
  return FilterStatus::Continue;
}

void Tracer::setEncoderFilterCallbacks(DubboFilters::EncoderFilterCallbacks&) {
}

FilterStatus Tracer::onMessageEncoded(MessageMetadataSharedPtr, ContextSharedPtr) {
  return FilterStatus::Continue;
}

void Tracer::onUpstreamData(Buffer::Instance&, bool) {
}

void Tracer::onEvent(Network::ConnectionEvent) {
}

Tracing::HttpTracerSharedPtr TracerConfigImpl::tracer() const { return tracer_; }

const Http::TracingConnectionManagerConfig* TracerConfigImpl::tracingConfig() const {
  return tracing_config_.get();
}

Tracing::OperationName TracerConfigImpl::operationName() const {
  return tracing_config_->operation_name_;
}

const Tracing::CustomTagMap* TracerConfigImpl::customTags() const {
  return &tracing_config_->custom_tags_; // http can specify tags with route filters
}

bool TracerConfigImpl::verbose() const { return tracing_config_->verbose_; }

uint32_t TracerConfigImpl::maxPathTagLength() const {
  return tracing_config_->max_path_tag_length_;
}

} // namespace Tracer
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
