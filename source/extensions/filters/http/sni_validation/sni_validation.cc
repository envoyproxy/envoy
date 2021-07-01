#include <string>
#include <vector>

#include "envoy/http/codes.h"

#include "source/common/http/utility.h"
#include "source/common/router/config_impl.h"
#include "source/extensions/filters/http/san_validation/san_validation.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SanValidation {

Http::FilterHeadersStatus SanValidationFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                             bool) {
  config->stats().enforced_.inc();

  decoder_callbacks_->sendLocalReply(
      421, "misredirected",
      [this, config](Http::HeaderMap& headers) {
        config->responseHeadersParser().evaluateHeaders(headers, decoder_callbacks_->streamInfo());
      },
      absl::nullopt, "misredirected");
  decoder_callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::RateLimited);

  return Http::FilterHeadersStatus::StopIteration;
}

} // namespace SanValidation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy