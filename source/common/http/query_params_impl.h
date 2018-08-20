#pragma once

#include "envoy/http/query_params.h"

namespace Envoy {
namespace Http {
namespace Utility {

class QueryParamsImpl : QueryParams {
public:
  QueryParamsImpl(absl::string_view);
  ~QueryParamsImpl();

  QueryParamsMap parseQueryString(absl::string_view) override;
  absl::string_view queryParamsToString(const QueryParamsMap&) override;
  QueryParamsMap getQueryParams() override;
  absl::string_view getURL() override;
  const QueryParamsMap::const_iterator find(const std::string key) const override;
  const QueryParamsMap::const_iterator end() const override;

private:
  QueryParamsMap query_params_;
  absl::string_view url_;
};

} // namespace Utility
} // namespace Http
} // namespace Envoy
