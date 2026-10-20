#include "source/extensions/http/ai_filters/route_selector/filter.h"

#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter_state.h"
#include "source/extensions/filters/http/ai_protocol_manager/llm_protocol_conversion.h"
#include "source/extensions/http/ai_filters/model_router/route_wrapper.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RouteSelector {

namespace {

const Http::LowerCaseString& anthropicVersionHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "anthropic-version");
}

} // namespace

namespace FilterStateKeys = HttpFilters::AiProtocolManager::FilterStateKeys;
using HttpFilters::AiProtocolManager::LlmBinding;
using HttpFilters::AiProtocolManager::LlmBindingConstSharedPtr;
using HttpFilters::AiProtocolManager::LlmBindingObject;
using HttpFilters::AiProtocolManager::LLMProtocol;
using HttpFilters::AiProtocolManager::LlmProtocolFilterState;
using HttpFilters::AiProtocolManager::protocolFromProto;

RouteSelectorFilterConfig::RouteSelectorFilterConfig(
    const envoy::extensions::http::ai_filters::route_selector::v3::RouteSelector&,
    Stats::Scope& scope)
    : stats_(RouteSelectorFilterStats{ALL_ROUTE_SELECTOR_FILTER_STATS(
          POOL_COUNTER_PREFIX(scope, "ai_protocol_manager.route_selector."))}) {}

RouteSelectorFilter::RouteSelectorFilter(
    RouteSelectorFilterConfigSharedPtr config,
    const HttpFilters::AiProtocolManager::AiFilterContext& context)
    : config_(std::move(config)), context_(context) {}

absl::Status
RouteSelectorFilter::decodeSync(HttpFilters::AiProtocolManager::AiRequest&,
                                HttpFilters::AiProtocolManager::LocalReplier reply_locally) {
  const StreamInfo::FilterStateSharedPtr& filter_state = context_.stream_info.filterState();

  StreamInfo::FilterState::ObjectsPtr shared = filter_state->objectsSharedWithUpstreamConnection();
  auto routes = filter_state->getDataReadOnly<ModelRouter::LlmRoutesFilterStateObject>(
      FilterStateKeys::ResolvedRoutes);

  if (!routes) {
    std::move(reply_locally)(Http::Code::InternalServerError, "LLM routes not found");
    config_->stats().invalid_route_data_.inc();
    return absl::OkStatus();
  }

  int current_retry = -1;
  std::string hdr(context_.request_headers.getEnvoyAttemptCountValue());
  if (!absl::SimpleAtoi(hdr, &current_retry) || current_retry <= 0 ||
      current_retry - 1 >= routes->proto().routes_size()) {
    std::move(reply_locally)(Http::Code::InternalServerError, "Invalid LLM request attempt");
    config_->stats().invalid_route_data_.inc();
    return absl::OkStatus();
  }

  const envoy::data::ai::v3::LlmRoute& route = routes->proto().routes(current_retry - 1);
  LLMProtocol protocol = protocolFromProto(route.llm_protocol());

  switch (protocol) {
  case LLMProtocol::Unspecified:
    std::move(reply_locally)(Http::Code::InternalServerError, "Invalid LLM protocol");
    config_->stats().invalid_route_data_.inc();
    return absl::OkStatus();
  case LLMProtocol::AnthropicMessages:
    context_.request_headers.addReference(anthropicVersionHeader(), "2023-06-01");
    break;
  default:
    break;
  }

  filter_state->setData(FilterStateKeys::LlmProtocolResponse,
                        std::make_shared<LlmProtocolFilterState>(protocol),
                        StreamInfo::FilterState::LifeSpan::FilterChain);

  filter_state->setData(FilterStateKeys::ModelResolved,
                        std::make_shared<Router::StringAccessorImpl>(route.model()),
                        StreamInfo::FilterState::LifeSpan::FilterChain);

  if (route.has_binding()) {
    absl::StatusOr<LlmBindingConstSharedPtr> binding = LlmBinding::create(route.binding());
    if (!binding.ok()) {
      std::move(reply_locally)(Http::Code::InternalServerError,
                               fmt::format("Invalid LLM binding: {}", binding.status().message()));
      config_->stats().invalid_route_data_.inc();
      return absl::OkStatus();
    }

    filter_state->setData(FilterStateKeys::BindingResponse,
                          std::make_shared<LlmBindingObject>(*binding),
                          StreamInfo::FilterState::LifeSpan::FilterChain);
  }

  if (!route.api_key().empty()) {
    context_.request_headers.setReference(Http::CustomHeaders::get().Authorization,
                                          route.api_key());
  }

  config_->stats().success_.inc();

  return absl::OkStatus();
}

} // namespace RouteSelector
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
