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

  // Only the most specific route config is used.
  // TODO(wbpcode): It's possible to traverse all the route configs to merge the header mutations
  // in the future.
  route_config_ =
      Http::Utility::resolveMostSpecificPerFilterConfig<PerRouteHeaderMutation>(decoder_callbacks_);

  if (route_config_ != nullptr) {
    route_config_->mutations().mutateRequestHeaders(headers, ctx, decoder_callbacks_->streamInfo());
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus HeaderMutation::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  Formatter::HttpFormatterContext ctx{encoder_callbacks_->requestHeaders().ptr(), &headers};
  config_->mutations().mutateResponseHeaders(headers, ctx, encoder_callbacks_->streamInfo());

  if (route_config_ == nullptr) {
    // If we haven't already resolved the route config, do so now.
    route_config_ = Http::Utility::resolveMostSpecificPerFilterConfig<PerRouteHeaderMutation>(
        encoder_callbacks_);
  }

  if (route_config_ != nullptr) {
    route_config_->mutations().mutateResponseHeaders(headers, ctx,
                                                     encoder_callbacks_->streamInfo());
  }

  return Http::FilterHeadersStatus::Continue;
}

} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
