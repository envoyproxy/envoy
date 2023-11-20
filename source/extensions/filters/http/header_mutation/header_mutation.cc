#include "source/extensions/filters/http/header_mutation/header_mutation.h"

#include <cstdint>
#include <memory>

#include "source/common/config/utility.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderMutation {

void Mutations::mutateRequestHeaders(Http::HeaderMap& headers,
                                     const Formatter::HttpFormatterContext& ctx,
                                     const StreamInfo::StreamInfo& stream_info) const {
  request_mutations_.evaluateHeaders(headers, ctx, stream_info);
}

void Mutations::mutateResponseHeaders(Http::HeaderMap& headers,
                                      const Formatter::HttpFormatterContext& ctx,
                                      const StreamInfo::StreamInfo& stream_info) const {
  response_mutations_.evaluateHeaders(headers, ctx, stream_info);
}

PerRouteHeaderMutation::PerRouteHeaderMutation(const PerRouteProtoConfig& config)
    : mutations_(config.mutations()) {}

HeaderMutationConfig::HeaderMutationConfig(const ProtoConfig& config)
    : mutations_(config.mutations()) {}

Http::FilterHeadersStatus HeaderMutation::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  Formatter::HttpFormatterContext ctx{&headers};
  config_->mutations().mutateRequestHeaders(headers, ctx, decoder_callbacks_->streamInfo());

  // Traverse through all route configs to retrieve all available header mutations.
  route_configs_ = Http::Utility::getAllPerFilterConfig<PerRouteHeaderMutation>(decoder_callbacks_);

  for (const auto* route_config : route_configs_) {
    ASSERT(route_config != nullptr);
    route_config->mutations().mutateRequestHeaders(headers, ctx, decoder_callbacks_->streamInfo());
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus HeaderMutation::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  Formatter::HttpFormatterContext ctx{encoder_callbacks_->requestHeaders().ptr(), &headers};
  config_->mutations().mutateResponseHeaders(headers, ctx, encoder_callbacks_->streamInfo());

  // If we haven't already traversed the route configs, do so now.
  if (route_configs_.empty()) {
    route_configs_ =
        Http::Utility::getAllPerFilterConfig<PerRouteHeaderMutation>(encoder_callbacks_);
  }

  for (const auto* route_config : route_configs_) {
    ASSERT(route_config != nullptr);
    route_config->mutations().mutateResponseHeaders(headers, ctx, encoder_callbacks_->streamInfo());
  }

  return Http::FilterHeadersStatus::Continue;
}

} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
