#include "library/common/extensions/filters/http/local_error/filter.h"

#include "envoy/server/filter_config.h"

#include "library/common/http/header_utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalError {

Http::LocalErrorStatus LocalErrorFilter::onLocalReply(const LocalReplyData& reply) {
  ENVOY_LOG(trace, "LocalErrorFilter::onLocalReply({}, {})", static_cast<uint64_t>(reply.code_),
            reply.details_);
  ASSERT(decoder_callbacks_);
  auto& info = decoder_callbacks_->streamInfo();
  // TODO(goaway): set responseCode in upstream Envoy when responseCodDetails are set.
  // ASSERT(static_cast<uint32_t>(reply.code_) == info.responseCode());
  // TODO(goaway): follow up on the underscore discrepancy between these values.
  // ASSERT(reply.details_ == info.responseCodeDetails().value());
  info.setResponseCode(static_cast<uint32_t>(reply.code_));

  return Http::Utility::statusForOnLocalReply(reply, decoder_callbacks_->streamInfo());
}

} // namespace LocalError
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
