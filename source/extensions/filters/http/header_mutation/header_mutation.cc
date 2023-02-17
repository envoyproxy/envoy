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

PerRouteHeaderMutation::PerRouteHeaderMutation(const PerRouteProtoConfig& config)
    : decoder_mutations_(config.decoder_mutations()),
      encoder_mutations_(config.encoder_mutations()) {}

void PerRouteHeaderMutation::mutateDecoderHeaders(Http::RequestHeaderMap& request_headers,
                                                  const StreamInfo::StreamInfo& stream_info) const {
  decoder_mutations_.evaluateHeaders(request_headers, request_headers,
                                     *Http::StaticEmptyHeaders::get().response_headers,
                                     stream_info);
}

void PerRouteHeaderMutation::mutateEncoderHeaders(const Http::RequestHeaderMap& request_headers,
                                                  Http::ResponseHeaderMap& response_headers,
                                                  const StreamInfo::StreamInfo& stream_info) const {
  encoder_mutations_.evaluateHeaders(response_headers, request_headers, response_headers,
                                     stream_info);
}

Http::FilterHeadersStatus HeaderMutation::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  route_config_ =
      Http::Utility::resolveMostSpecificPerFilterConfig<PerRouteHeaderMutation>(decoder_callbacks_);

  if (route_config_ != nullptr) {
    route_config_->mutateDecoderHeaders(headers, decoder_callbacks_->streamInfo());
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus HeaderMutation::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  if (route_config_ == nullptr) {
    // If we haven't already resolved the route config, do so now.
    route_config_ = Http::Utility::resolveMostSpecificPerFilterConfig<PerRouteHeaderMutation>(
        decoder_callbacks_);
  }

  if (route_config_ != nullptr) {
    route_config_->mutateEncoderHeaders(encoder_callbacks_->streamInfo().getRequestHeaders() ==
                                                nullptr
                                            ? *Http::StaticEmptyHeaders::get().request_headers
                                            : *encoder_callbacks_->streamInfo().getRequestHeaders(),
                                        headers, decoder_callbacks_->streamInfo());
  }

  return Http::FilterHeadersStatus::Continue;
}

} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
