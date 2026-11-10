#include "source/extensions/http/ai_filters/route_selector/filter.h"

#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter_state.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RouteSelector {

namespace {

const Http::LowerCaseString& googleApiKeyHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "x-goog-api-key");
}

const Http::LowerCaseString& apiKeyHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "x-api-key");
}

} // namespace

namespace FilterStateKeys = HttpFilters::AiProtocolManager::FilterStateKeys;
using HttpFilters::AiProtocolManager::LlmBindingObject;
using HttpFilters::AiProtocolManager::LLMProtocol;
using HttpFilters::AiProtocolManager::LlmProtocolFilterState;
using HttpFilters::AiProtocolManager::LlmRoute;
using HttpFilters::AiProtocolManager::LlmRoutesFilterStateObject;

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

  auto routes =
      filter_state->getDataReadOnly<LlmRoutesFilterStateObject>(FilterStateKeys::ResolvedRoutes);

  if (!routes) {
    std::move(reply_locally)(Http::Code::InternalServerError, "LLM routes not found");
    config_->stats().invalid_route_data_.inc();
    return absl::OkStatus();
  }

  std::optional<uint32_t> attempt = context_.stream_info.attemptCount();
  if (!attempt || *attempt == 0 || *attempt - 1 >= routes->routes().size()) {
    std::move(reply_locally)(Http::Code::InternalServerError, "Invalid LLM request attempt");
    config_->stats().invalid_route_data_.inc();
    return absl::OkStatus();
  }

  const size_t route_index = *attempt - 1;
  const LlmRoute& route = routes->routes()[route_index];
  if (route.llm_protocol == LLMProtocol::Unspecified) {
    std::move(reply_locally)(Http::Code::InternalServerError, "Invalid LLM protocol");
    config_->stats().invalid_route_data_.inc();
    return absl::OkStatus();
  }

  // Cleanup headers we manage in this filter.
  context_.request_headers.remove(Http::CustomHeaders::get().Authorization);
  context_.request_headers.remove(googleApiKeyHeader());
  context_.request_headers.remove(apiKeyHeader());

  if (!route.api_key.empty()) {
    switch (route.llm_protocol) {
    case LLMProtocol::GeminiGenerateContent:
      context_.request_headers.setReferenceKey(googleApiKeyHeader(), route.api_key);
      break;
    default:
      context_.request_headers.setReferenceKey(Http::CustomHeaders::get().Authorization,
                                               route.api_key);
      break;
    }
  }

  Envoy::Http::Utility::updateAuthority(context_.request_headers, route.host_and_port, false,
                                        false);

  filter_state->setData(FilterStateKeys::LlmProtocolResponse,
                        std::make_shared<LlmProtocolFilterState>(route.llm_protocol),
                        StreamInfo::FilterState::LifeSpan::FilterChain);

  filter_state->setData(FilterStateKeys::ModelResolved,
                        std::make_shared<Router::StringAccessorImpl>(route.model),
                        StreamInfo::FilterState::LifeSpan::FilterChain);

  filter_state->setData(FilterStateKeys::BindingResponse,
                        route.binding != nullptr ? std::make_shared<LlmBindingObject>(route.binding)
                                                 : nullptr,
                        StreamInfo::FilterState::LifeSpan::FilterChain);

  config_->stats().success_.inc();

  return absl::OkStatus();
}

} // namespace RouteSelector
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
