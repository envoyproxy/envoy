#include "source/extensions/filters/http/original_src/original_src.h"

#include "source/common/common/assert.h"
#include "source/extensions/filters/common/original_src/socket_option_factory.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OriginalSrc {

OriginalSrcFilter::OriginalSrcFilter(const Config& config) : config_(config) {}

void OriginalSrcFilter::onDestroy() {}

Http::FilterHeadersStatus OriginalSrcFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  const auto downstream_address =
      callbacks_->streamInfo().downstreamAddressProvider().remoteAddress();
  ASSERT(downstream_address);

  if (downstream_address->type() != Network::Address::Type::Ip) {
    // Nothing we can do with this.
    return Http::FilterHeadersStatus::Continue;
  }

  ENVOY_LOG(debug,
            "Got a new connection in the original_src filter for address {}. Marking with {}",
            downstream_address->asString(), config_.mark());

  const auto options_to_add = Filters::Common::OriginalSrc::buildOriginalSrcOptions(
      std::move(downstream_address), config_.mark());
  callbacks_->addUpstreamSocketOptions(options_to_add);
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus OriginalSrcFilter::decodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus OriginalSrcFilter::decodeTrailers(Http::RequestTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void OriginalSrcFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

} // namespace OriginalSrc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
