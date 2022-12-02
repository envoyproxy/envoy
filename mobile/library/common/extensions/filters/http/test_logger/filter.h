#pragma once

#include "envoy/http/filter.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "library/common/extensions/filters/http/test_logger/filter.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestLogger {

// A filter that emits an ENVOY_EVENT log, used for testing event tracker integration.
class Filter : public Envoy::Http::PassThroughFilter, public Logger::Loggable<Logger::Id::filter> {
public:
  Filter() { ENVOY_LOG_EVENT(debug, "event_name", "test log"); }
};

} // namespace TestLogger
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
