#include "source/common/http/matching/inputs.h"

#include "envoy/registry/registry.h"

#include "common/http/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Http {
namespace Matching {

Matcher::DataInputGetResult HttpRequestCookiesDataInput::get(const HttpMatchingData& data) {
  const auto maybe_headers = data.requestHeaders();

  if (!maybe_headers) {
    return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::nullopt};
  }

  const auto ret =
      Http::Utility::parseCookieValues(*maybe_headers, cookie_name_, 0, false /* reversed_order */);
  if (ret.size() == 0) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
  }
  result_storage_ = absl::StrJoin(ret, ",");
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, result_storage_};
}

REGISTER_FACTORY(HttpRequestHeadersDataInputFactory, Matcher::DataInputFactory<HttpMatchingData>);
REGISTER_FACTORY(HttpResponseHeadersDataInputFactory, Matcher::DataInputFactory<HttpMatchingData>);
REGISTER_FACTORY(HttpRequestTrailersDataInputFactory, Matcher::DataInputFactory<HttpMatchingData>);
REGISTER_FACTORY(HttpResponseTrailersDataInputFactory, Matcher::DataInputFactory<HttpMatchingData>);

REGISTER_FACTORY(HttpRequestCookiesDataInputFactory, Matcher::DataInputFactory<HttpMatchingData>);
} // namespace Matching
} // namespace Http
} // namespace Envoy
