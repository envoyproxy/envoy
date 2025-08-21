#include "test/common/http/filters/test_accessor/filter.h"

#include "envoy/server/filter_config.h"

#include "source/common/common/assert.h"

#include "library/common/bridge/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestAccessor {

TestAccessorFilterConfig::TestAccessorFilterConfig(
    const envoymobile::extensions::filters::http::test_accessor::TestAccessor& proto_config)
    : accessor_(static_cast<envoy_string_accessor*>(
          Api::External::retrieveApi(proto_config.accessor_name()))),
      expected_string_(proto_config.expected_string()) {}

Http::FilterHeadersStatus TestAccessorFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  RELEASE_ASSERT(config_->expectedString() ==
                     Bridge::Utility::copyToString(
                         config_->accessor()->get_string(config_->accessor()->context)),
                 "accessed string is not equal to expected string");
  return Http::FilterHeadersStatus::Continue;
}

} // namespace TestAccessor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
