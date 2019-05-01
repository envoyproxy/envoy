#include "extensions/filters/http/original_src/original_src.h"

#include "common/common/assert.h"

#include "extensions/filters/common/original_src/socket_option_factory.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OriginalSrc {

OriginalSrcFilter::OriginalSrcFilter(const Config& config) : config_(config) {}

void OriginalSrcFilter::onDestroy() {}

Http::FilterHeadersStatus OriginalSrcFilter::decodeHeaders(Http::HeaderMap&, bool end_stream) {

  // We wait until the end of all the headers to ensure any that affect that downstream remote
  // address have been parsed. Further, if there's no downstream connection, we can't do anything
  // so just continue.
  if (!end_stream || !callbacks_->connection()) {
    return Http::FilterHeadersStatus::Continue;
  }

  auto connection_options = callbacks_->connection()->socketOptions();
  auto address = callbacks_->streamInfo().downstreamRemoteAddress();
  ASSERT(address);

  ENVOY_LOG(debug,
            "Got a new connection in the original_src filter for address {}. Marking with {}",
            address->asString(), config_.mark());

  if (address->type() != Network::Address::Type::Ip) {
    // Nothing we can do with this.
    return Http::FilterHeadersStatus::Continue;
  }
  auto options_to_add =
      Filters::Common::OriginalSrc::buildOriginalSrcOptions(std::move(address), config_.mark());
  Network::Socket::appendOptions(connection_options, options_to_add);
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus OriginalSrcFilter::decodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus OriginalSrcFilter::decodeTrailers(Http::HeaderMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void OriginalSrcFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

} // namespace OriginalSrc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
