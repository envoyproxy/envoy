#include "source/common/http/matching/inputs.h"

#include "envoy/registry/registry.h"

#include "source/common/http/utility.h"

namespace Envoy {
namespace Http {
namespace Matching {

Matcher::DataInputGetResult
HttpRequestQueryParamsDataInput::get(const HttpMatchingData& data) const {
  const auto maybe_headers = data.requestHeaders();

  if (!maybe_headers) {
    return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::nullopt};
  }

  const auto ret = maybe_headers->Path();
  if (!ret) {
    return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::nullopt};
  }

  Utility::QueryParams params =
      Http::Utility::parseAndDecodeQueryString(ret->value().getStringView());

  auto ItParam = params.find(query_param_);
  if (ItParam == params.end()) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          std::move(ItParam->second)};
}

REGISTER_FACTORY(HttpRequestHeadersDataInputFactory, Matcher::DataInputFactory<HttpMatchingData>);
REGISTER_FACTORY(HttpResponseHeadersDataInputFactory, Matcher::DataInputFactory<HttpMatchingData>);
REGISTER_FACTORY(HttpRequestTrailersDataInputFactory, Matcher::DataInputFactory<HttpMatchingData>);
REGISTER_FACTORY(HttpResponseTrailersDataInputFactory, Matcher::DataInputFactory<HttpMatchingData>);

REGISTER_FACTORY(HttpRequestQueryParamsDataInputFactory,
                 Matcher::DataInputFactory<HttpMatchingData>);

} // namespace Matching
} // namespace Http
} // namespace Envoy
