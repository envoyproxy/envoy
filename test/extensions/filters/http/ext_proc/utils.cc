#include "test/extensions/filters/http/ext_proc/utils.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

const absl::flat_hash_set<std::string> ExtProcTestUtility::ignoredHeaders() {
  CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<std::string>, "x-request-id",
                         "x-envoy-upstream-service-time", "x-envoy-expected-rq-timeout-ms");
}

bool ExtProcTestUtility::headerProtosEqualIgnoreOrder(
    const Http::HeaderMap& expected, const envoy::config::core::v3::HeaderMap& actual) {
  // Comparing header maps is hard because they have duplicates in them.
  // So we're going to turn them into a HeaderMap and let Envoy do the work.
  Http::TestRequestHeaderMapImpl actual_headers;
  for (const auto& header : actual.headers()) {
    if (!ignoredHeaders().contains(header.key())) {
      actual_headers.addCopy(header.key(), header.raw_value());
    }
  }
  return TestUtility::headerMapEqualIgnoreOrder(expected, actual_headers);
}

envoy::config::core::v3::HeaderValue makeHeaderValue(const std::string& key,
                                                     const std::string& value) {
  envoy::config::core::v3::HeaderValue v;
  v.set_key(key);
  v.set_value(value);
  return v;
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
