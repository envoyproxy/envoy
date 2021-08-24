#pragma once

#include "envoy/http/filter.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "library/common/extensions/filters/http/local_error/filter.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalError {

/**
 * Filter to assert expectations on HTTP requests.
 */
class LocalErrorFilter final : public Http::PassThroughFilter,
                               public Logger::Loggable<Logger::Id::filter> {
public:
  // StreamFilterBase
  Http::LocalErrorStatus onLocalReply(const LocalReplyData&) override;
};

} // namespace LocalError
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
