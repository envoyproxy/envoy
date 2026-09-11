#include "source/extensions/filters/ai/request_info/filter.h"

#include <utility>

#include "envoy/data/ai/v3/request_info.pb.h"

#include "source/common/coroutine/status_macros.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/ai/request_info/extractor.h"
#include "source/extensions/filters/http/ai_protocol_manager/api_protocol_conversion.h"

#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RequestInfo {

using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::AiRequestPropagator;
using HttpFilters::AiProtocolManager::AiRequestPtr;
using HttpFilters::AiProtocolManager::AiRequestReceiver;
using HttpFilters::AiProtocolManager::LocalReplier;

namespace {

constexpr absl::string_view DefaultMetadataNamespace{"envoy.ai.request_info"};

envoy::data::ai::v3::RequestInfo toProto(const RequestAttributes& attrs) {
  envoy::data::ai::v3::RequestInfo typed;
  typed.set_api_protocol(HttpFilters::AiProtocolManager::protocolToProto(attrs.api_protocol));
  typed.set_model(attrs.model);
  if (attrs.stream.has_value()) {
    typed.mutable_stream()->set_value(attrs.stream.value());
  }
  if (attrs.max_output_tokens.has_value()) {
    typed.mutable_max_output_tokens()->set_value(attrs.max_output_tokens.value());
  }
  if (attrs.message_count.has_value()) {
    typed.mutable_message_count()->set_value(attrs.message_count.value());
  }
  if (attrs.tool_count.has_value()) {
    typed.mutable_tool_count()->set_value(attrs.tool_count.value());
  }
  return typed;
}

} // namespace

RequestInfoFilterConfig::RequestInfoFilterConfig(
    const envoy::extensions::filters::ai::request_info::v3::RequestInfo& proto, Stats::Scope& scope)
    : stats_(RequestInfoFilterStats{ALL_REQUEST_INFO_FILTER_STATS(
          POOL_COUNTER_PREFIX(scope, "ai_protocol_manager.request_info."))}),
      metadata_namespace_(proto.metadata_namespace().empty() ? std::string(DefaultMetadataNamespace)
                                                             : proto.metadata_namespace()) {}

RequestInfoFilter::RequestInfoFilter(RequestInfoFilterConfigSharedPtr config,
                                     const AiFilterContext& context)
    : config_(std::move(config)), context_(context) {}

Coroutine::Task<absl::Status> RequestInfoFilter::decode(AiRequestReceiver receive_request,
                                                        AiRequestPropagator propagate_request,
                                                        LocalReplier) {
  ASSIGN_OR_CO_RETURN(AiRequestPtr request, co_await std::move(receive_request)());
  publish(request->request_index().json());
  co_return co_await std::move(propagate_request)(std::move(request));
}

void RequestInfoFilter::publish(const nlohmann::json& json) {
  StreamInfo::StreamInfo& stream_info = context_.stream_info;
  if (stream_info.dynamicMetadata().typed_filter_metadata().contains(
          config_->metadataNamespace())) {
    ENVOY_LOG(debug, "request_info: namespace {} already published; skipping",
              config_->metadataNamespace());
    config_->stats().duplicate_.inc();
    return;
  }

  const RequestAttributes attrs = extractRequestAttributes(context_.request_protocol, json,
                                                           context_.request_headers.getPathValue());
  Protobuf::Any typed_any;
  MessageUtil::packFrom(typed_any, toProto(attrs));
  stream_info.setDynamicTypedMetadata(config_->metadataNamespace(), typed_any);

  config_->stats().published_.inc();
  if (attrs.malformed) {
    config_->stats().partial_.inc();
  }
  ENVOY_LOG(trace, "request_info: published to namespace {}", config_->metadataNamespace());
}

} // namespace RequestInfo
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
